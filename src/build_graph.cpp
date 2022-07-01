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

// Return `[finished, ?file_size]`, the behavior is undefined
// unless `tok.getRawIdentifier() == "pragma"`.
std::pair<bool, std::optional<
                    boost::units::quantity<boost::units::information::info>>>
parse_pragma(clang::Lexer &lex, clang::Token &tok, unsigned &token_count) {
  assert(tok.getRawIdentifier() == "pragma");
  if (lex.LexFromRawLexer(tok)) {
    ++token_count;
    return {true, std::nullopt};
  }
  if (!tok.is(clang::tok::raw_identifier)) {
    return {false, std::nullopt};
  }

  if (tok.getRawIdentifier() != "override_file_size") {
    return {false, std::nullopt};
  }

  if (lex.LexFromRawLexer(tok)) {
    ++token_count;
    return {true, std::nullopt};
  }

  if (!tok.is(clang::tok::l_paren)) {
    return {false, std::nullopt};
  }

  if (lex.LexFromRawLexer(tok)) {
    ++token_count;
    return {true, std::nullopt};
  }

  if (!tok.is(clang::tok::numeric_constant)) {
    return {false, std::nullopt};
  }

  const std::string_view file_size_str(tok.getLiteralData(), tok.getLength());
  std::size_t file_size;
  const auto [ptr, ec] =
      std::from_chars(file_size_str.data(),
                      file_size_str.data() + file_size_str.size(), file_size);
  if (ec == std::errc()) {
    return {false, file_size * boost::units::information::bytes};
  }

  // TODO, finish the closing paren

  return {false, std::nullopt};
}

} // namespace

llvm::Expected<build_graph::result>
build_graph::from_dir(std::filesystem::path source_dir,
                      std::span<const std::filesystem::path> include_dirs,
                      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
                      std::function<file_type(std::string_view)> file_type) {

  source_dir = std::filesystem::absolute(source_dir);
  assert(std::all_of(include_dirs.begin(), include_dirs.end(),
                     std::mem_fn(&std::filesystem::path::is_absolute)));
  std::deque<std::filesystem::path> sources;
  std::error_code ec;
  std::vector<std::string> directories;
  directories.push_back(source_dir.string());
  const llvm::vfs::directory_iterator end;
  while (!directories.empty()) {
    const std::string dir_copy = std::move(directories.back());
    directories.pop_back();
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

  build_graph::result r;

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

  while (!sources.empty()) {
    const std::filesystem::path source = std::move(sources.front());
    sources.pop_front();
    llvm::ErrorOr<const clang::FileEntry *> opt_file_entry =
        file_manager->getFile(source.string());
    const clang::FileEntry *file_entry = opt_file_entry.get();
    auto const [it, inserted] =
        id_to_node.emplace(file_entry->getUniqueID(), empty);
    if (it->second.fully_processed) {
      continue;
    }

    if (inserted) {
      const bool is_external = false;
      it->second.v =
          add_vertex({source.lexically_relative(source_dir), is_external,
                      file_entry->getSize() * boost::units::information::bytes},
                     r.graph);
      r.sources.emplace_back(it->second.v);
    }

    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer =
        file_manager->getBufferForFile(file_entry);

    clang::Lexer lex(sm->getOrCreateFileID(file_entry, clang::SrcMgr::C_User),
                     **buffer, *sm, options);
    clang::Token tok;
    unsigned token_count = 0u;
    unsigned include_count = 0u;
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
                  file_manager->getDirectoryRef(source.string());
              const clang::DirectoryEntry *y =
                  *file_manager->getDirectory(source.parent_path().string());
              llvm::ArrayRef<std::pair<const clang::FileEntry *,
                                       const clang::DirectoryEntry *>>
                  includers = {{file_entry, y}};
              llvm::Optional<clang::FileEntryRef> file_ref =
                  header_search.LookupFile(
                      include_text, tok.getLastLoc(), is_angled, nullptr,
                      current_directory, includers, nullptr, nullptr, nullptr,
                      nullptr, nullptr, nullptr);
              if (file_ref) {
                auto const [to_it, inserted] =
                    id_to_node.emplace(file_ref->getUniqueID(), empty);
                if (inserted) {
                  sources.emplace_back(std::string(
                      file_ref.getValue().getFileEntry().getName()));
                  if (!to_it->second.fully_processed) {
                    const std::filesystem::path &p = sources.back();
                    const bool is_external = current_directory != nullptr;
                    const std::filesystem::path dir =
                        is_external ? std::filesystem::path(std::string(
                                          current_directory->getName()))
                                    : source_dir;
                    to_it->second.v =
                        add_vertex({p.lexically_relative(dir), is_external,
                                    static_cast<double>(file_ref->getSize()) *
                                        boost::units::information::bytes},
                                   r.graph);
                  }
                }
                const std::string include(tok.getLiteralData(),
                                          tok.getLength());
                add_edge(
                    it->second.v, to_it->second.v,
                    {include, sm->getSpellingLineNumber(tok.getLocation())},
                    r.graph);
              } else {
                r.missing_files.emplace_back(include_text);
              }
              ++include_count;
            }
          } else if (tok.getRawIdentifier() == "pragma") {
            const auto [finished, file_size] =
                parse_pragma(lex, tok, token_count);
            if (file_size.has_value()) {
              r.graph[it->second.v].file_size = *file_size;
            }
            if (finished) {
              break;
            }
          }
        }
      }
    }
    it->second.fully_processed = true;
  }
  return r;
}

llvm::Expected<build_graph::result>
build_graph::from_dir(const std::filesystem::path &source_dir,
                      std::initializer_list<std::filesystem::path> include_dirs,
                      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
                      std::function<file_type(std::string_view)> file_type) {
  return from_dir(source_dir,
                  std::span(include_dirs.begin(), include_dirs.end()), fs,
                  file_type);
}

} // namespace IncludeGuardian
