#include "build_graph.hpp"

#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/PreprocessorOptions.h>

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

clang::IgnoringDiagConsumer s_ignore;

struct Hasher {
  std::size_t operator()(const llvm::sys::fs::UniqueID key) const noexcept {
    // NOTE: Not a good hash, but out devices should probably be the same
    return key.getFile() ^ key.getDevice();
  }
};

struct FileState {
  Graph::vertex_descriptor v;
  std::filesystem::path angled_rel; //< This is the relative path of `v`
                                    //< compared to the last angled include seen
  bool fully_processed = false;
  bool file_size_overridden = false;
  bool token_count_overridden = false;
};

using UniqueIdToNode =
    std::unordered_map<llvm::sys::fs::UniqueID, FileState, Hasher>;

struct IncludeScanner : public clang::PPCallbacks {
  clang::SourceManager *m_sm;
  UniqueIdToNode &m_id_to_node;
  build_graph::result &m_r;
  const std::function<build_graph::file_type(std::string_view)> &m_file_type;
  std::vector<UniqueIdToNode::iterator> m_stack;
  unsigned m_accounted_for_token_count;
  clang::Preprocessor *m_pp;

  // Add the number of preprocessing tokens seen since the last time
  // this function was called to the top file on our stack.
  // Apply the costs (preprocessing tokens/file size) as we leave the
  // specified `finished` file.
  void apply_costs(const clang::FileEntry *finished) {
    if (!m_stack.back()->second.token_count_overridden) {
      m_r.graph[m_stack.back()->second.v].underlying_cost.token_count +=
          m_pp->getTokenCount() - m_accounted_for_token_count;
    }
    m_accounted_for_token_count = m_pp->getTokenCount();
    if (!m_stack.back()->second.file_size_overridden) {
      m_r.graph[m_stack.back()->second.v].underlying_cost.file_size +=
          finished->getSize() * boost::units::information::bytes;
    }
  }

public:
  IncludeScanner(
      const std::function<build_graph::file_type(std::string_view)> &file_type,
      build_graph::result &r, UniqueIdToNode &id_to_node,
      clang::Preprocessor &pp)
      : m_r(r), m_sm(&pp.getSourceManager()), m_id_to_node(id_to_node),
        m_file_type(file_type), m_pp(&pp), m_accounted_for_token_count{0u} {}

  void FileChanged(clang::SourceLocation Loc, FileChangeReason Reason,
                   clang::SrcMgr::CharacteristicKind FileType,
                   clang::FileID OptionalPrevFID) final {
    switch (Reason) {
    case FileChangeReason::EnterFile: {
      const clang::FileID fileID = m_sm->getFileID(Loc);

      const clang::FileEntry *file = m_sm->getFileEntryForID(fileID);
      if (!file) {
        // Ignore if this is the predefines
        return;
      }

      const auto it = m_id_to_node.find(file->getUniqueID());

      // We should already have added this in `InclusionDirective` or
      // it is a source file that was already added to the graph
      assert(it != m_id_to_node.end());

      m_stack.push_back(it);
      return;
    }
    case FileChangeReason::ExitFile: {
      if (const clang::FileEntry *file =
              m_sm->getFileEntryForID(OptionalPrevFID)) {
        // If we are unguarded, then don't set the 'fully_processed' stuff
        // and move the total cost into the includer.
        const bool guarded =
            m_pp->getHeaderSearchInfo().isFileMultipleIncludeGuarded(file);
        if (guarded) {
          // If we're guarded then we are fully processed and we won't need
          // to enter this file again.
          m_stack.back()->second.fully_processed = true;

          // Apply the costs to ourselves
          apply_costs(file);
          m_stack.pop_back();
        } else {
          // If we're not guarded then we `pop_back` and attribute the token
          // count to the file that included us.
          m_stack.pop_back();
          apply_costs(file);
        }
      }
      return;
    }
    case FileChangeReason::RenameFile:
      return;
    case FileChangeReason::SystemHeaderPragma:
      return;
    }
  }

  void FileSkipped(const clang::FileEntryRef &SkippedFile,
                   const clang::Token &FilenameTok,
                   clang::SrcMgr::CharacteristicKind FileType) final {}

  void InclusionDirective(clang::SourceLocation HashLoc,
                          const clang::Token &IncludeTok,
                          clang::StringRef FileName, bool IsAngled,
                          clang::CharSourceRange FilenameRange,
                          const clang::FileEntry *File,
                          clang::StringRef SearchPath,
                          clang::StringRef RelativePath,
                          const clang::Module *Imported,
                          clang::SrcMgr::CharacteristicKind FileType) final {

    if (m_stack.back()->second.fully_processed) {
      return;
    }

    if (!File) {
      // File does not exist
      m_r.missing_includes.emplace(FileName.str());
      return;
    }
    const clang::FileID fileID = m_sm->getFileID(HashLoc);
    const char open = IsAngled ? '<' : '"';
    std::string include(&open, 1);
    include.insert(include.cend(), FileName.begin(), FileName.end());
    const char close = IsAngled ? '>' : '"';
    include.insert(include.cend(), &close, &close + 1);
    const Graph::vertex_descriptor from = m_stack.back()->second.v;
    auto const [it, inserted] =
        m_id_to_node.emplace(File->getUniqueID(), empty);
    if (inserted) {
      const unsigned internal_incoming = 0u;
      const bool is_external = clang::SrcMgr::isSystem(FileType);

      // If we don't have an angled include, try and build up the relative
      // path from the first angled include.
      const std::filesystem::path p =
          (IsAngled ? std::filesystem::path(RelativePath.str())
                    : m_stack.back()->second.angled_rel / RelativePath.str())
              .make_preferred()
              .lexically_normal();

      // We are also precompiled if the header including us is
      // precompiled
      const bool is_precompiled =
          (!m_stack.empty() &&
           m_r.graph[m_stack.back()->second.v].is_precompiled) ||
          m_file_type(p.string()) == build_graph::file_type::precompiled_header;

      it->second.v =
          add_vertex({p, is_external, internal_incoming,
                      cost{0ull, 0.0 * boost::units::information::bytes},
                      std::nullopt, is_precompiled},
                     m_r.graph);

      // If we have a non-angled include, then make it relative to the previous
      // path we are storing.
      const std::filesystem::path relative_path(RelativePath.str());
      it->second.angled_rel =
          (IsAngled ? relative_path
                    : m_stack.back()->second.angled_rel / relative_path)
              .parent_path();
    }

    const Graph::vertex_descriptor to = it->second.v;

    const clang::FileID fromFileID = m_sm->getFileID(HashLoc);
    const clang::FileEntry *fromFile = m_sm->getFileEntryForID(fromFileID);
    const bool is_from_predefines = fromFile == nullptr;

    // To differentiate includes coming from the predefines we use 0 instead
    const unsigned line_number =
        is_from_predefines ? 0 : m_sm->getSpellingLineNumber(HashLoc);

    // Guess at whether this include is the interface for our source file
    // TODO: Check we are a source file
    const bool is_component =
        m_r.graph[from].path.stem() == m_r.graph[to].path.stem();

    // If we are in the predefines section assume this include cannot be
    // removed
    const bool is_removable = !is_from_predefines && !is_component;

    add_edge(from, to, {include, line_number, is_removable}, m_r.graph);
    m_r.graph[to].internal_incoming += !m_r.graph[from].is_external;

    // If we haven't already guessed at a header-source connection
    // then add it in.
    if (is_component && !m_r.graph[from].component.has_value()) {
      m_r.graph[to].component = from;
      m_r.graph[from].component = to;
    }
  }

  /// Callback invoked when start reading any pragma directive.
  void PragmaDirective(clang::SourceLocation Loc,
                       clang::PragmaIntroducerKind Introducer) final {
    clang::SmallVector<char> buffer;
    const clang::StringRef pragma_text =
        clang::Lexer::getSpelling(Loc, buffer, *m_sm, m_pp->getLangOpts());

    // NOTE: Our files should all be null-terminated strings
    {
      const clang::StringRef prefix = "#pragma override_file_size(";
      if (std::equal(prefix.begin(), prefix.end(), pragma_text.data())) {
        std::size_t file_size;
        const char *start = pragma_text.data() + prefix.size();
        const char *end = std::strchr(start, ')');
        const auto [ptr, ec] = std::from_chars(start, end, file_size);
        if (ec == std::errc()) {
          m_stack.back()->second.file_size_overridden = true;
          const Graph::vertex_descriptor v = m_stack.back()->second.v;
          m_r.graph[v].underlying_cost.file_size =
              file_size * boost::units::information::bytes;
        }
      }
    }

    {
      const clang::StringRef prefix = "#pragma override_token_count(";
      if (std::equal(prefix.begin(), prefix.end(), pragma_text.data())) {
        std::size_t token_count;
        const char *start = pragma_text.data() + prefix.size();
        const char *end = std::strchr(start, ')');
        const auto [ptr, ec] = std::from_chars(start, end, token_count);
        if (ec == std::errc()) {
          m_stack.back()->second.token_count_overridden = true;
          const Graph::vertex_descriptor v = m_stack.back()->second.v;
          m_r.graph[v].underlying_cost.token_count = token_count;
        }
      }
    }
  }

  void EndOfMainFile() final {
    assert(m_stack.size() == 1);
    apply_costs(m_sm->getFileEntryForID(m_sm->getMainFileID()));
  }
};

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

  auto pp_opts = std::make_shared<clang::PreprocessorOptions>();
  auto diag_ids = llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>();
  auto diag_options = llvm::makeIntrusiveRefCnt<clang::DiagnosticOptions>();
  auto diagnostics = llvm::makeIntrusiveRefCnt<clang::DiagnosticsEngine>(
      diag_ids, diag_options);
  diagnostics->setClient(&s_ignore, false);
  clang::LangOptions options;
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
  clang::HeaderSearch header_search(search_options, *sm, *diagnostics, options,
                                    target_info.get());
  for (const std::filesystem::path &include : include_dirs) {
    const bool is_framework = false;
    llvm::Expected<clang::DirectoryEntryRef> dir_ref =
        file_manager->getDirectoryRef(include.string());
    if (dir_ref) {
      clang::DirectoryLookup dir(
          *dir_ref, clang::SrcMgr::CharacteristicKind::C_System, is_framework);
      const bool is_angled = true;
      header_search.AddSearchPath(dir, is_angled);
    }
  }

  clang::TrivialModuleLoader module_loader;
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

  const std::string predefines = std::accumulate(
      forced_includes.begin(), forced_includes.end(), std::string(),
      [](std::string &&acc, const std::filesystem::path &forced_include) {
        acc += "#include \"";
        acc += forced_include.string();
        acc += "\"\n";
        return std::move(acc);
      });

  std::unordered_map<llvm::sys::fs::UniqueID, FileState, Hasher> id_to_node;
  build_graph::result r;

  // Process all sources
  for (const std::filesystem::path &source : sources) {
    // TODO: Check `opt_file_entry` exists
    llvm::ErrorOr<const clang::FileEntry *> opt_file_entry =
        file_manager->getFile(source.string());

    auto const [it, inserted] =
        id_to_node.emplace(opt_file_entry.get()->getUniqueID(), empty);
    if (inserted) {
      const bool is_external = false;
      const unsigned internal_incoming = 0u;
      const bool is_precompiled = false;
      const std::filesystem::path rel = source.lexically_relative(source_dir);
      it->second.v =
          add_vertex({rel, is_external, internal_incoming,
                      cost{0ull, 0.0 * boost::units::information::bytes},
                      std::nullopt, is_precompiled},
                     r.graph);
      it->second.angled_rel = rel.parent_path();
      r.sources.push_back(it->second.v);
    }
    sm->setMainFileID(
        sm->getOrCreateFileID(opt_file_entry.get(), clang::SrcMgr::C_User));
    clang::Preprocessor pp(pp_opts, *diagnostics, options, *sm, header_search,
                           module_loader);
    pp.setPredefines(predefines);
    pp.Initialize(*target_info);

    auto callback =
        std::make_unique<IncludeScanner>(file_type, r, id_to_node, pp);
    IncludeScanner &scanner = *callback;
    pp.addPPCallbacks(std::move(callback));

    // Don't use any pragma handlers other than our raw `PPCallbacks` one
    pp.IgnorePragmas();

    pp.EnterMainSourceFile();
    clang::Token token;
    do {
      pp.Lex(token);
    } while (token.isNot(clang::tok::eof));
    pp.EndSourceFile();
    r.sources.push_back(scanner.m_stack.back()->second.v);
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
