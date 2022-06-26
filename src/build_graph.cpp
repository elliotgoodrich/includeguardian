#include "build_graph.hpp"

#include <clang/AST/ASTConsumer.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>

#include <charconv>
#include <filesystem>
#include <numeric>
#include <string>
#include <utility>

namespace IncludeGuardian {

namespace {

class TestCompilationDatabase : public clang::tooling::CompilationDatabase {
public:
  std::filesystem::path m_working_directory;
  std::vector<std::filesystem::path> m_sources;
  std::span<const std::filesystem::path> m_include_dirs;

  TestCompilationDatabase(const std::filesystem::path &working_directory)
      : m_working_directory(working_directory), m_sources(), m_include_dirs() {}

  /// Returns all compile commands in which the specified file was
  /// compiled.
  ///
  /// This includes compile commands that span multiple source files.
  /// For example, consider a project with the following compilations:
  /// $ clang++ -o test a.cc b.cc t.cc
  /// $ clang++ -o production a.cc b.cc -DPRODUCTION
  /// A compilation database representing the project would return both command
  /// lines for a.cc and b.cc and only the first command line for t.cc.
  std::vector<clang::tooling::CompileCommand>
  getCompileCommands(clang::StringRef FilePath) const final {
    // Note for the preprocessor we do not need `-o` flags and it is
    // specifically stripped out by an `ArgumentAdjuster`.
    using namespace std::string_literals;

    std::vector<std::string> things;
    things.emplace_back("/usr/bin/clang++");
    things.emplace_back(FilePath.str());
    for (const std::filesystem::path &include : m_include_dirs) {
      things.emplace_back("-I");
      things.emplace_back(include.string());
    }
    return {{m_working_directory.string(), FilePath, std::move(things), "out"}};
  }

  /// Returns the list of all files available in the compilation database.
  ///
  /// By default, returns nothing. Implementations should override this if they
  /// can enumerate their source files.
  std::vector<std::string> getAllFiles() const final {
    std::vector<std::string> result(m_sources.size());
    std::transform(m_sources.cbegin(), m_sources.cend(), result.begin(),
                   [](const std::filesystem::path &p) { return p.string(); });
    return result;
  }
};

clang::IgnoringDiagConsumer s_ignore;

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

class IncludeScanner : public clang::PPCallbacks {
  clang::SourceManager *m_sm;
  UniqueIdToNode &m_id_to_node;
  build_graph::result &m_r;
  const clang::LangOptions &m_options;
  std::vector<UniqueIdToNode::iterator> m_stack;

  UniqueIdToNode::iterator lookup_or_insert(const clang::FileEntry *file) {
    const llvm::sys::fs::UniqueID id =
        file ? file->getUniqueID() : llvm::sys::fs::UniqueID();
    auto const [it, inserted] = m_id_to_node.emplace(id, empty);
    if (file && inserted) {
      const std::filesystem::path p = file->getName().str();
      it->second.v =
          add_vertex({p.lexically_normal(),
                      file->getSize() * boost::units::information::bytes},
                     m_r.graph);
    }

    return it;
  }

public:
  IncludeScanner(clang::SourceManager &sm, const clang::LangOptions &options,
                 build_graph::result &r, UniqueIdToNode &id_to_node)
      : m_r(r), m_sm(&sm), m_id_to_node(id_to_node), m_options(options) {}

  void FileChanged(clang::SourceLocation Loc, FileChangeReason Reason,
                   clang::SrcMgr::CharacteristicKind FileType,
                   clang::FileID OptionalPrevFID) final {

    const clang::FileID fileID = m_sm->getFileID(Loc);
    const clang::FileEntry *file = m_sm->getFileEntryForID(fileID);

    // If `file` is `nullptr` then we are processing some magic predefine
    // file from clang that we can ignore for now.
    if (!file) {
      return;
    }

    const auto it = lookup_or_insert(file);
    if (Reason == FileChangeReason::ExitFile) {
      // There are few quirks of clang's preprocessor that I have had to work
      // around here.  First, we do not always get an `ExitFile` notification
      // for files that do not have any include directives.  Second, we get the
      // following series of callbacks for source files
      //
      // EnterFile main.cpp
      // EnterFile (nullptr clang::FileEntry)
      // RenameFile (nullptr clang::FileEntry)
      // ExitFile (nullptr clang::FileEntry)
      // ExitFile main.cpp
      // ... rest of the callbacks
      // ExitFile main.cpp
      //
      // Which means that we actually would have an empty stack if we blindly
      // followed what we are told.
      //
      // To hack around both of these issues we have to do a search backwards in
      // `m_stack` to perhaps pop off multiple elements - but we never pop off
      // the last element and `assert(m_stack.size() == 1)` at the end.
      const auto found = std::find(m_stack.rbegin(), m_stack.rend(), it);
      const auto b = std::max(found.base(), m_stack.begin() + 1);
      for (auto i = b; i != m_stack.end(); ++i) {
        m_stack.back()->second.fully_processed = true;
      }
      m_stack.erase(found.base(), m_stack.end());
      return;
    }

    if (Reason == FileChangeReason::RenameFile) {
      return;
    }

    if (m_stack.empty()) {
      m_r.sources.push_back(it->second.v);
    }

    m_stack.push_back(it);
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

#ifndef NDEBUG
    const clang::FileID fromFileID = m_sm->getFileID(HashLoc);
    const clang::FileEntry *fromFile = m_sm->getFileEntryForID(fromFileID);
#endif
    assert(m_id_to_node.find(fromFile->getUniqueID()) == m_stack.back());
    if (m_stack.back()->second.fully_processed) {
      return;
    }

    const clang::FileID fileID = m_sm->getFileID(HashLoc);
    const char open = IsAngled ? '<' : '"';
    std::string include(&open, 1);
    include.insert(include.cend(), FileName.begin(), FileName.end());
    const char close = IsAngled ? '>' : '"';
    include.insert(include.cend(), &close, &close + 1);
    if (File) {
      add_edge(m_stack.back()->second.v, lookup_or_insert(File)->second.v,
               {include, m_sm->getSpellingLineNumber(HashLoc)}, m_r.graph);
    } else {
      // File does not exist
      m_r.missing_files.emplace_back(FileName.str());
    }
  }

  /// Callback invoked when start reading any pragma directive.
  void PragmaDirective(clang::SourceLocation Loc,
                       clang::PragmaIntroducerKind Introducer) final {
    clang::SmallVector<char> buffer;
    const clang::StringRef pragma_text =
        clang::Lexer::getSpelling(Loc, buffer, *m_sm, m_options);

    // NOTE: Our files should all be null-terminated strings
    const clang::StringRef prefix = "#pragma override_file_size(";
    if (std::equal(prefix.begin(), prefix.end(), pragma_text.data())) {
      std::size_t file_size;
      const char *start = pragma_text.data() + prefix.size();
      const char *end = std::strchr(start, ')');
      const auto [ptr, ec] = std::from_chars(start, end, file_size);
      if (ec == std::errc()) {
        const Graph::vertex_descriptor v = m_stack.back()->second.v;
        m_r.graph[v].file_size = file_size * boost::units::information::bytes;
      }
    }
  }

  void EndOfMainFile() final { assert(m_stack.size() == 1); }
};

class ExpensiveAction : public clang::PreprocessOnlyAction {
  clang::ast_matchers::MatchFinder m_f;
  clang::CompilerInstance *m_ci;
  build_graph::result &m_r;
  UniqueIdToNode &m_id_to_node;

public:
  ExpensiveAction(build_graph::result &r, UniqueIdToNode &id_to_node)
      : m_f(), m_ci(nullptr), m_r(r), m_id_to_node(id_to_node) {}

  bool BeginInvocation(clang::CompilerInstance &ci) final {
    ci.getDiagnostics().setClient(&s_ignore, /*TakeOwnership=*/false);
    ci.getDiagnostics().setErrorLimit(0u);
    m_ci = &ci;
    return true;
  }

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &CI,
                    clang::StringRef InFile) final {
    return m_f.newASTConsumer();
  }

  void ExecuteAction() final {
    getCompilerInstance().getPreprocessor().addPPCallbacks(
        std::make_unique<IncludeScanner>(
            m_ci->getSourceManager(), m_ci->getLangOpts(), m_r, m_id_to_node));

    clang::PreprocessOnlyAction::ExecuteAction();
  }
};

/// This component is a concrete implementation of a `FrontEndActionFactory`
/// that will output the include directives along with the total file size
/// that would be saved if it was deleted.
class find_graph_factory : public clang::tooling::FrontendActionFactory {
  build_graph::result &m_r;
  UniqueIdToNode &m_id_to_node;

public:
  /// Create a `print_graph_factory`.
  find_graph_factory(build_graph::result &r, UniqueIdToNode &id_to_node)
      : m_r(r), m_id_to_node(id_to_node) {}

  /// Returns a new `clang::FrontendAction`.
  std::unique_ptr<clang::FrontendAction> create() final {
    return std::make_unique<ExpensiveAction>(m_r, m_id_to_node);
  }
};

} // namespace

llvm::Expected<build_graph::result> build_graph::from_compilation_db(
    const clang::tooling::CompilationDatabase &compilation_db,
    std::span<const std::filesystem::path> source_paths,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs) {

  std::vector<std::string> source_path_strings(source_paths.size());
  std::transform(source_paths.begin(), source_paths.end(),
                 source_path_strings.begin(),
                 [](const std::filesystem::path &p) { return p.string(); });
  clang::tooling::ClangTool tool(
      compilation_db, source_path_strings,
      std::make_shared<clang::PCHContainerOperations>(), fs);

  UniqueIdToNode id_to_node;
  result r;
  find_graph_factory f(r, id_to_node);
  const int rc = tool.run(&f);
  if (rc != 0) {
    return llvm::createStringError(std::error_code(rc, std::generic_category()),
                                   "oops");
  }
  return r;
}

llvm::Expected<build_graph::result>
build_graph::from_dir(const llvm::Twine &dir,
                      std::span<const std::filesystem::path> include_dirs,
                      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
                      std::function<file_type(std::string_view)> file_type) {
  const std::filesystem::path wd(dir.str());

  TestCompilationDatabase db({dir.str()});
  db.m_include_dirs = include_dirs;
  std::error_code ec;
  std::vector<std::string> directories;
  directories.push_back(dir.str());
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

        // Make sure all filenames are relative.  This allows us to generate
        // results that won't include any "private" information.  For example,
        // if the user's name appears in the absolute path, they may be less
        // inclined to share it.  It also means that results generated for the
        // same project will be identical across computers.
        std::filesystem::path path(it->path().str());
        db.m_sources.emplace_back(path.lexically_relative(wd));
      }
      it.increment(ec);
    }
  }

  return from_compilation_db(db, db.m_sources, fs);
}

} // namespace IncludeGuardian
