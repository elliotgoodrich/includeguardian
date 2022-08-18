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
  bool fully_processed = false;
  bool token_count_overridden = false;
};

using UniqueIdToNode =
    std::unordered_map<llvm::sys::fs::UniqueID, FileState, Hasher>;

// For the specified path `file`, if it is located within `source_dir` then
// return its relative path within it and `true`.  Otherwise look at
// each element of `external_dirs` in turn and return the relative path for
// the first element that `file` is found in and `false`.  Otherwise return
// `{file, true}`.
std::pair<std::filesystem::path, bool /*is_external*/>
rel_to_one_of(const std::filesystem::path &file,
              const std::filesystem::path &source_dir,
              std::span<const std::filesystem::path> external_dirs) {
  {
    const auto [file_it, source_it] = std::mismatch(
        file.begin(), file.end(), source_dir.begin(), source_dir.end());
    if (source_it == source_dir.end()) {
      return {file.lexically_relative(source_dir).make_preferred(),
              /*is external=*/false};
    }
  }
  for (const std::filesystem::path &dir : external_dirs) {
    const auto [file_it, dir_it] =
        std::mismatch(file.begin(), file.end(), dir.begin(), dir.end());
    if (dir_it == dir.end()) {
      return {file.lexically_relative(dir).make_preferred(),
              /*is external=*/true};
    }
  }

  return {file, /*is external=*/true};
}

struct IncludeScanner : public clang::PPCallbacks {
  clang::SourceManager *m_sm;
  UniqueIdToNode &m_id_to_node;
  build_graph::result &m_r;
  const clang::LangOptions &m_options;
  const std::filesystem::path &m_source_dir;
  std::span<const std::filesystem::path> m_include_dirs;
  const std::function<build_graph::file_type(std::string_view)> &m_file_type;
  std::vector<UniqueIdToNode::iterator> m_stack;

  UniqueIdToNode::iterator lookup_or_insert(const clang::FileEntry *file) {
    const llvm::sys::fs::UniqueID id =
        file ? file->getUniqueID() : llvm::sys::fs::UniqueID();
    auto const [it, inserted] = m_id_to_node.emplace(id, empty);
    if (file && inserted) {
      const unsigned internal_incoming = 0u;
      const auto [p, is_external] =
          rel_to_one_of(file->getName().str(), m_source_dir, m_include_dirs);

      // We are also precompiled if the header including us is
      // precompiled
      const bool is_precompiled =
          (!m_stack.empty() &&
           m_r.graph[m_stack.back()->second.v].is_precompiled) ||
          m_file_type(p.string()) == build_graph::file_type::precompiled_header;

      it->second.v = add_vertex(
          {p, is_external, internal_incoming,
           cost{0ull, file->getSize() * boost::units::information::bytes},
           std::nullopt, is_precompiled},
          m_r.graph);
    }

    return it;
  }

public:
  IncludeScanner(
      clang::SourceManager &sm, const clang::LangOptions &options,
      const std::filesystem::path &source_dir,
      std::span<const std::filesystem::path> include_dirs,
      const std::function<build_graph::file_type(std::string_view)> &file_type,
      build_graph::result &r, UniqueIdToNode &id_to_node)
      : m_r(r), m_sm(&sm), m_id_to_node(id_to_node), m_options(options),
        m_source_dir(source_dir), m_include_dirs(include_dirs),
        m_file_type(file_type) {}

  long long &current_token_counter() {
    if (m_stack.back()->second.token_count_overridden) {
      static long long dummy = 0;
      dummy = 0;
      return dummy;
    }
    return m_r.graph[m_stack.back()->second.v].underlying_cost.token_count;
  }

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
      const auto it = lookup_or_insert(file);
      if (m_stack.empty()) {
        m_r.sources.push_back(it->second.v);
      }

      m_stack.push_back(it);
      return;
    }
    case FileChangeReason::ExitFile: {
      if (m_sm->getFileEntryForID(OptionalPrevFID)) {
        // Ignore the predefines
        m_stack.back()->second.fully_processed = true;
        m_stack.pop_back();
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
    const Graph::vertex_descriptor to = lookup_or_insert(File)->second.v;

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
    auto l = m_r.graph[from].path;
    auto r = m_r.graph[to].path;

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
        clang::Lexer::getSpelling(Loc, buffer, *m_sm, m_options);

    // NOTE: Our files should all be null-terminated strings
    {
      const clang::StringRef prefix = "#pragma override_file_size(";
      if (std::equal(prefix.begin(), prefix.end(), pragma_text.data())) {
        std::size_t file_size;
        const char *start = pragma_text.data() + prefix.size();
        const char *end = std::strchr(start, ')');
        const auto [ptr, ec] = std::from_chars(start, end, file_size);
        if (ec == std::errc()) {
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

  void EndOfMainFile() final { assert(m_stack.size() == 1); }
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
          *dir_ref, clang::SrcMgr::CharacteristicKind::C_User, is_framework);
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
    llvm::ErrorOr<const clang::FileEntry *> opt_file_entry =
        file_manager->getFile(source.string());
    sm->setMainFileID(
        sm->getOrCreateFileID(opt_file_entry.get(), clang::SrcMgr::C_User));
    clang::Preprocessor pp(pp_opts, *diagnostics, options, *sm, header_search,
                           module_loader);
    pp.setPredefines(predefines);
    pp.Initialize(*target_info);

    auto callback = std::make_unique<IncludeScanner>(
        *sm, options, source_dir, include_dirs, file_type, r, id_to_node);
    IncludeScanner &scanner = *callback;
    pp.addPPCallbacks(std::move(callback));

    // Don't use any pragma handlers other than our raw `PPCallbacks` one
    pp.IgnorePragmas();

    pp.EnterMainSourceFile();
    clang::Token token;
    do {
      pp.Lex(token);
      ++scanner.current_token_counter();
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
