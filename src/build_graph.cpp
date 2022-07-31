#include "build_graph.hpp"

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticIDs.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Lex/HeaderSearchOptions.h>
#include <clang/Lex/Lexer.h>

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Triple.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/Host.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <memory>
#include <numeric>
#include <string>
#include <utility>

namespace IncludeGuardian {

namespace {

const Graph::vertex_descriptor empty =
    boost::graph_traits<Graph>::null_vertex();

struct Hasher {
  std::size_t operator()(const llvm::sys::fs::UniqueID key) const noexcept {
    // NOTE: Not a good hash, but out devices should probably be the same
    return key.getFile() ^ key.getDevice();
  }
};

struct FileState {
  Graph::vertex_descriptor v;
  bool fully_processed = false;
};

using UniqueIdToNode =
    std::unordered_map<llvm::sys::fs::UniqueID, FileState, Hasher>;

struct Arguments {
  const std::filesystem::path &source_dir;
  UniqueIdToNode id_to_node;

  Arguments(const std::filesystem::path &source_dir)
      : source_dir(source_dir), id_to_node() {}
};

// Update `f`s `file_size` or `token_count` based on the `#pragma` stored in the
// specified `tok`.  Return whether this is the end of the file.
bool parse_pragma(clang::Lexer &lex, clang::Token &tok, unsigned &token_count,
                  file_node &f, bool &token_count_overriden) {
  assert(tok.getRawIdentifier() == "pragma");
  if (lex.LexFromRawLexer(tok)) {
    ++token_count;
    return true;
  }
  if (!tok.is(clang::tok::raw_identifier)) {
    return false;
  }

  const llvm::StringRef pragmas[] = {"override_file_size",
                                     "override_token_count"};
  const int type = std::find(std::begin(pragmas), std::end(pragmas),
                             tok.getRawIdentifier()) -
                   std::begin(pragmas);
  if (type == 2) {
    return false;
  }

  if (lex.LexFromRawLexer(tok)) {
    ++token_count;
    return true;
  }

  if (!tok.is(clang::tok::l_paren)) {
    return false;
  }

  if (lex.LexFromRawLexer(tok)) {
    ++token_count;
    return true;
  }

  if (!tok.is(clang::tok::numeric_constant)) {
    return false;
  }

  const std::string_view arg_str(tok.getLiteralData(), tok.getLength());
  std::size_t arg;
  const auto [ptr, ec] =
      std::from_chars(arg_str.data(), arg_str.data() + arg_str.size(), arg);
  if (ec == std::errc()) {
    if (type == 0) {
      f.underlying_cost.file_size = arg * boost::units::information::bytes;
    } else {
      f.underlying_cost.token_count = arg;
      token_count_overriden = true;
    }
    return false;
  }

  // TODO, finish the closing paren

  return false;
}

Graph::vertex_descriptor process(
    std::filesystem::path start, const std::filesystem::path &source_dir,
    std::unordered_map<llvm::sys::fs::UniqueID, FileState, Hasher> &id_to_node,
    const std::function<build_graph::file_type(std::string_view)> &file_type,
    Graph &graph, clang::FileManager &file_manager, clang::SourceManager &sm,
    clang::HeaderSearch &header_search,
    std::set<std::string> missing_includes) {
  std::deque<std::filesystem::path> todo;
  todo.push_back(start);
  while (!todo.empty()) {
    std::filesystem::path file = todo.front();
    todo.pop_front();
    llvm::ErrorOr<const clang::FileEntry *> opt_file_entry =
        file_manager.getFile(file.string());
    const clang::FileEntry *file_entry = opt_file_entry.get();
    // FIXME, `it` becomes invalidated when we `emplace` later on
    auto const [it, inserted] =
        id_to_node.emplace(file_entry->getUniqueID(), empty);
    if (it->second.fully_processed) {
      continue;
    }

    if (inserted) {
      // The files passed to `process` shouldn't be external files
      // for now
      const bool is_external = false;
      const bool is_precompiled = file_type(file.string()) ==
                                  build_graph::file_type::precompiled_header;
      it->second.v = add_vertex(
          {file.lexically_relative(source_dir), is_external, 0u,
           cost{0u, file_entry->getSize() * boost::units::information::bytes},
           std::nullopt, is_precompiled},
          graph);
    }

    bool token_count_overriden = false;

    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer =
        file_manager.getBufferForFile(file_entry);

    const clang::LangOptions options;
    clang::Lexer lex(sm.getOrCreateFileID(file_entry, clang::SrcMgr::C_User),
                     **buffer, sm, options);
    clang::Token tok;
    unsigned token_count = 0u;
    while (!lex.LexFromRawLexer(tok)) {
      ++token_count;
      if (tok.is(clang::tok::hash) && tok.isAtStartOfLine()) {
        if (lex.LexFromRawLexer(tok)) {
          ++token_count;
          break;
        }
        if (tok.is(clang::tok::raw_identifier)) {
          if (tok.getRawIdentifier() == "include") {
            lex.LexIncludeFilename(tok);
            if (tok.getKind() == clang::tok::header_name) {
              const std::string_view include_text(tok.getLiteralData() + 1,
                                                  tok.getLength() - 2);
              const bool is_angled = *tok.getLiteralData() == '<';
              const clang::DirectoryLookup *current_directory = nullptr;
              llvm::Expected<clang::DirectoryEntryRef> current_dir_ref =
                  file_manager.getDirectoryRef(file.string());
              const clang::DirectoryEntry *y =
                  *file_manager.getDirectory(file.parent_path().string());
              const std::pair<const clang::FileEntry *,
                              const clang::DirectoryEntry *>
                  includer(file_entry, y);
              llvm::Optional<clang::FileEntryRef> file_ref =
                  header_search.LookupFile(
                      include_text, tok.getLastLoc(), is_angled, nullptr,
                      current_directory, llvm::ArrayRef(includer), nullptr,
                      nullptr, nullptr, nullptr, nullptr, nullptr);
              if (file_ref) {
                auto const [to_it, inserted] =
                    id_to_node.emplace(file_ref->getUniqueID(), empty);
                if (inserted) {
                  todo.emplace_back(std::string(
                      file_ref.getValue().getFileEntry().getName()));
                  if (!to_it->second.fully_processed) {
                    const std::filesystem::path &p = todo.back();
                    const bool is_external = current_directory != nullptr;
                    const std::filesystem::path dir =
                        is_external ? std::filesystem::path(std::string(
                                          current_directory->getName()))
                                    : source_dir;
                    // We are also precompiled if the header including us is
                    // precompiled
                    const bool is_precompiled =
                        graph[it->second.v].is_precompiled ||
                        file_type(p.string()) ==
                            build_graph::file_type::precompiled_header;
                    to_it->second.v = add_vertex(
                        {p.lexically_relative(dir), is_external, 0u,
                         cost{
                             0u,
                             static_cast<double>(file_ref->getSize()) *
                                 boost::units::information::bytes,
                         },
                         std::nullopt, is_precompiled},
                        graph);
                  }
                }
                const std::string include(tok.getLiteralData(),
                                          tok.getLength());
                const std::filesystem::path p(
                    file_ref.getValue().getFileEntry().getName().str());
                const bool is_removable = p.stem() != file.stem();
                add_edge(it->second.v, to_it->second.v,
                         {include, sm.getSpellingLineNumber(tok.getLocation()),
                          is_removable},
                         graph);
                if (!graph[it->second.v].is_external) {
                  ++graph[to_it->second.v].internal_incoming;
                }

                // If we haven't already guessed at a header-source connection
                // then add it in.
                if (!is_removable &&
                    !graph[it->second.v].component.has_value()) {
                  graph[to_it->second.v].component = it->second.v;
                  graph[it->second.v].component = to_it->second.v;
                }
              } else {
                missing_includes.emplace(tok.getLiteralData(), tok.getLength());
              }
            }
          } else if (tok.getRawIdentifier() == "pragma") {
            if (parse_pragma(lex, tok, token_count, graph[it->second.v],
                             token_count_overriden)) {
              break;
            }
          }
        }
      }
      if (!token_count_overriden) {
        graph[it->second.v].underlying_cost.token_count = token_count;
      }
      it->second.fully_processed = true;
    }
  }

  llvm::ErrorOr<const clang::FileEntry *> opt_file_entry =
      file_manager.getFile(start.string());
  const clang::FileEntry *file_entry = opt_file_entry.get();
  return id_to_node[file_entry->getUniqueID()].v;
}

} // namespace

llvm::Expected<build_graph::result>
build_graph::from_dir(std::filesystem::path source_dir,
                      std::span<const std::filesystem::path> include_dirs,
                      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
                      std::function<file_type(std::string_view)> file_type,
                      std::span<const std::filesystem::path> forced_includes) {

  source_dir = std::filesystem::absolute(source_dir);
  assert(std::all_of(include_dirs.begin(), include_dirs.end(),
                     std::mem_fn(&std::filesystem::path::is_absolute)));
  assert(std::all_of(forced_includes.begin(), forced_includes.end(),
                     std::mem_fn(&std::filesystem::path::is_absolute)));

  auto diag_ids = llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>();
  auto diag_options = llvm::makeIntrusiveRefCnt<clang::DiagnosticOptions>();
  auto diagnostics = llvm::makeIntrusiveRefCnt<clang::DiagnosticsEngine>(
      diag_ids, diag_options);
  clang::FileSystemOptions file_options;
  auto file_manager =
      llvm::makeIntrusiveRefCnt<clang::FileManager>(file_options, fs);
  auto sm = llvm::makeIntrusiveRefCnt<clang::SourceManager>(*diagnostics,
                                                            *file_manager);
  auto target_options = std::make_shared<clang::TargetOptions>();
  target_options->Triple = llvm::sys::getDefaultTargetTriple();
  target_options->CodeModel = "default";
  llvm::IntrusiveRefCntPtr<clang::TargetInfo> target_info =
      clang::TargetInfo::CreateTargetInfo(*diagnostics, target_options);
  auto search_options = std::make_shared<clang::HeaderSearchOptions>();

  // Populate our `HeaderSearch` with the given include directories
  const clang::LangOptions options;
  clang::HeaderSearch header_search(search_options, *sm, *diagnostics, options,
                                    target_info.get());
  for (const std::filesystem::path &include : include_dirs) {
    const bool is_framework = false;
    llvm::Expected<clang::DirectoryEntryRef> dir_ref =
        file_manager->getDirectoryRef(include.string());
    if (dir_ref) {
      clang::DirectoryLookup dir(
          *dir_ref, clang::SrcMgr::CharacteristicKind::C_User, is_framework);
      const bool is_angled = true;
      header_search.AddSearchPath(dir, is_angled);
    }
  }

  std::unordered_map<llvm::sys::fs::UniqueID, FileState, Hasher> id_to_node;
  build_graph::result r;

  // Process all forced includes first and store the results
  std::vector<Graph::vertex_descriptor> extra_includes;
  for (const std::filesystem::path &forced_include : forced_includes) {
    extra_includes.emplace_back(process(forced_include, source_dir, id_to_node,
                                        file_type, r.graph, *file_manager, *sm,
                                        header_search, r.missing_includes));
  }

  // Find all sources within the given directories
  std::vector<std::filesystem::path> sources;

  std::vector<std::string> directories;
  directories.push_back(source_dir.string());
  const llvm::vfs::directory_iterator end;
  while (!directories.empty()) {
    const std::string dir_copy = std::move(directories.back());
    directories.pop_back();
    std::error_code ec;
    llvm::vfs::directory_iterator it = fs->dir_begin(dir_copy, ec);
    while (!ec && it != end) {
      if (it->type() == llvm::sys::fs::file_type::directory_file) {
        directories.push_back(it->path().str());
      } else if (it->type() == llvm::sys::fs::file_type::regular_file &&
                 file_type(it->path()) == file_type::source) {
        sources.emplace_back(it->path().str());
      }
      it.increment(ec);
    }
  }

  // Process all sources, making sure to pass in our forced includes
  for (const std::filesystem::path &source : sources) {
    r.sources.emplace_back(process(source, source_dir, id_to_node, file_type,
                                   r.graph, *file_manager, *sm, header_search,
                                   r.missing_includes));
  }

  return r;
}

llvm::Expected<build_graph::result> build_graph::from_dir(
    const std::filesystem::path &source_dir,
    std::initializer_list<std::filesystem::path> include_dirs,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
    std::function<file_type(std::string_view)> file_type,
    std::initializer_list<std::filesystem::path> forced_includes) {
  return from_dir(
      source_dir, std::span(include_dirs.begin(), include_dirs.end()), fs,
      file_type, std::span(forced_includes.begin(), forced_includes.end()));
}

} // namespace IncludeGuardian
