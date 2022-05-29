#include "build_graph.hpp"

#include <clang/AST/ASTConsumer.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>

#include <boost/graph/graphviz.hpp>

#include <charconv>
#include <filesystem>
#include <numeric>
#include <string>
#include <utility>

namespace IncludeGuardian {

namespace {

class IncludeScanner : public clang::PPCallbacks {
  clang::SourceManager *m_sm;
  std::unordered_map<unsigned, Graph::vertex_descriptor> m_lookup;
  Graph &m_graph;
  std::vector<Graph::vertex_descriptor> &m_out;
  const clang::LangOptions &m_options;

  Graph::vertex_descriptor get_vertex_desc(const clang::FileEntry *file) {
    const auto it = m_lookup.find(file->getUID());
    if (it != m_lookup.end()) {
      return it->second;
    } else {
      // Strip leading "./" from filenames that we get from clang
      const std::filesystem::path p =
          std::filesystem::path(file->getName().str()).lexically_relative("./");
      return m_lookup
          .emplace(file->getUID(),
                   add_vertex({p.string(), static_cast<std::size_t>(file->getSize())}, m_graph))
          .first->second;
    }
  }

public:
  IncludeScanner(clang::SourceManager &sm, const clang::LangOptions &options,
                 Graph &graph, std::vector<Graph::vertex_descriptor> &out)
      : m_graph(graph), m_sm(&sm), m_lookup(), m_out(out), m_options(options) {}

  void FileChanged(clang::SourceLocation Loc, FileChangeReason Reason,
                   clang::SrcMgr::CharacteristicKind FileType,
                   clang::FileID OptionalPrevFID) final {
    if (Reason == FileChangeReason::EnterFile) {
      if (m_sm->isInMainFile(Loc)) {
        // TODO: Avoid the extra lookup
        const clang::FileID fileID = m_sm->getFileID(Loc);
        if (const clang::FileEntry *file = m_sm->getFileEntryForID(fileID)) {
          const auto it = m_lookup.find(file->getUID());
          if (it == m_lookup.end()) {
            m_out.push_back(get_vertex_desc(file));
          }
        }
      }
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
    const clang::FileID fileID = m_sm->getFileID(HashLoc);
    const char open = IsAngled ? '<' : '"';
    std::string include(&open, 1);
    include.insert(include.cend(), FileName.begin(), FileName.end());
    const char close = IsAngled ? '>' : '"';
    include.insert(include.cend(), &close, &close + 1);
    add_edge(get_vertex_desc(m_sm->getFileEntryForID(fileID)),
             get_vertex_desc(File), {include}, m_graph);
  }

  /// Callback invoked when start reading any pragma directive.
  void PragmaDirective(clang::SourceLocation Loc,
                       clang::PragmaIntroducerKind Introducer) final {
    clang::SmallVector<char> buffer;
    const clang::StringRef pragma_text =
        clang::Lexer::getSpelling(Loc, buffer, *m_sm, m_options);
    const std::string_view prefix = "#pragma override_file_size(";

    // FIXME: There must be a better way to do this.  We are assuming that
    // `pragma_text` is null terminated.  I vaguely remember reading that
    // it is, but it's still ugly.
    if (std::equal(prefix.cbegin(), prefix.cend(), pragma_text.data())) {
      std::size_t file_size;
      const char *start = pragma_text.data() + prefix.size();
      const char *end = std::strchr(start, ')');
      const auto [ptr, ec] = std::from_chars(start, end, file_size);
      if (ec == std::errc()) {
        const clang::FileID fileID = m_sm->getFileID(Loc);
        if (const clang::FileEntry *file = m_sm->getFileEntryForID(fileID)) {
          const Graph::vertex_descriptor v =
              m_lookup.find(file->getUID())->second;
          m_graph[v].fileSizeInBytes = file_size;
        }
      }
    }
  }

  void EndOfMainFile() final {}
};

class ExpensiveAction : public clang::PreprocessOnlyAction {
  clang::ast_matchers::MatchFinder m_f;
  clang::CompilerInstance *m_ci = nullptr;
  std::vector<Graph::vertex_descriptor> &m_out;
  Graph &m_graph;

public:
  ExpensiveAction(Graph &graph, std::vector<Graph::vertex_descriptor> &out)
      : m_out(out), m_graph(graph) {}

  bool BeginInvocation(clang::CompilerInstance &ci) final {
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
        std::make_unique<IncludeScanner>(m_ci->getSourceManager(),
                                         m_ci->getLangOpts(), m_graph, m_out));

    clang::PreprocessOnlyAction::ExecuteAction();
  }
};

/// This component is a concrete implementation of a `FrontEndActionFactory`
/// that will output the include directives along with the total file size
/// that would be saved if it was deleted.
class find_graph_factory : public clang::tooling::FrontendActionFactory {
  Graph &m_graph;
  std::vector<Graph::vertex_descriptor> &m_out;

public:
  /// Create a `print_graph_factory`.
  find_graph_factory(Graph &graph, std::vector<Graph::vertex_descriptor> &out)
      : m_graph(graph), m_out(out) {}

  /// Returns a new `clang::FrontendAction`.
  std::unique_ptr<clang::FrontendAction> create() final {
    return std::make_unique<ExpensiveAction>(m_graph, m_out);
  }
};

} // namespace

llvm::Expected<std::pair<Graph, std::vector<Graph::vertex_descriptor>>>
build_graph::from_compilation_db(
    const clang::tooling::CompilationDatabase &compilation_db,
    std::span<const std::string> source_paths,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs) {
  clang::tooling::ClangTool tool(
      compilation_db,
      llvm::ArrayRef<std::string>(source_paths.data(), source_paths.size()),
      std::make_shared<clang::PCHContainerOperations>(), fs);
  std::pair<Graph, std::vector<Graph::vertex_descriptor>> result;
  find_graph_factory f(result.first, result.second);
  const int rc = tool.run(&f);
  if (rc != 0) {
    return llvm::createStringError(std::error_code(rc, std::generic_category()),
                                   "oops");
  }
  return result;
}

} // namespace IncludeGuardian
