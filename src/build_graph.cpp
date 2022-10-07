#include "build_graph.hpp"

#include <clang/AST/ASTConsumer.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticIDs.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Lex/HeaderSearchOptions.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

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

class FakeCompilationDatabase : public clang::tooling::CompilationDatabase {
public:
  std::filesystem::path m_working_directory;
  std::vector<std::filesystem::path> m_sources;
  std::span<
      const std::pair<std::filesystem::path, clang::SrcMgr::CharacteristicKind>>
      m_include_dirs;
  std::span<const std::filesystem::path> m_force_includes;

  FakeCompilationDatabase(const std::filesystem::path &working_directory)
      : m_working_directory(working_directory), m_force_includes(), m_sources(),
        m_include_dirs() {}

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
    for (const auto &[path, type] : m_include_dirs) {
      things.emplace_back(clang::SrcMgr::isSystem(type) ? "-isystem" : "-I");
      things.emplace_back(path.string());
    }
    for (const std::filesystem::path &forced : m_force_includes) {
      things.emplace_back("-include");
      things.emplace_back(forced.string());
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
  std::string replacement_contents; //< If we are `fully_processed` this will
                                    //< contain a C++ file that is equivalent
                                    //< when this is < included by another file
                                    //< (i.e. only the macro < definitions)
};

using UniqueIdToNode =
    std::unordered_map<llvm::sys::fs::UniqueID, FileState, Hasher>;

struct IncludeScanner : public clang::PPCallbacks {
  clang::SourceManager *m_sm;
  UniqueIdToNode &m_id_to_node;
  build_graph::result &m_r;
  std::function<build_graph::file_type(std::string_view)> m_file_type;
  std::vector<UniqueIdToNode::iterator> m_stack;
  unsigned m_accounted_for_token_count;
  clang::Preprocessor *m_pp;
  std::filesystem::path m_working_dir;
  bool m_replace_file_optimization;

  // Add the number of preprocessing tokens seen since the last time
  // this function was called to the top file on our stack.
  // Apply the costs (preprocessing tokens/file size) as we leave the
  // specified `finished` file.
  void apply_costs(const clang::FileEntry *finished) {
    FileState &state = m_stack.back()->second;
    if (!state.fully_processed) {
      if (!m_stack.back()->second.token_count_overridden) {
        m_r.graph[m_stack.back()->second.v].underlying_cost.token_count +=
            m_pp->getTokenCount() - m_accounted_for_token_count;
      }
      if (!m_stack.back()->second.file_size_overridden) {
        m_r.graph[m_stack.back()->second.v].underlying_cost.file_size +=
            finished->getSize() * boost::units::information::bytes;
      }
    }
    m_accounted_for_token_count = m_pp->getTokenCount();
  }

  // This function is taken from MacroPPCallbacks.cpp
  // Part of the LLVM Project, under the Apache License v2.0 with LLVM
  // Exceptions.
  // See https://llvm.org/LICENSE.txt for license information.
  // SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
  static void writeMacroDefinition(const clang::IdentifierInfo &II,
                                   const clang::MacroInfo &MI,
                                   clang::Preprocessor &PP,
                                   clang::raw_ostream &Name,
                                   clang::raw_ostream &Value) {
    Name << II.getName();

    if (MI.isFunctionLike()) {
      Name << '(';
      if (!MI.param_empty()) {
        auto AI = MI.param_begin(), E = MI.param_end();
        for (; AI + 1 != E; ++AI) {
          Name << (*AI)->getName();
          Name << ',';
        }

        // Last argument.
        if ((*AI)->getName() == "__VA_ARGS__")
          Name << "...";
        else
          Name << (*AI)->getName();
      }

      if (MI.isGNUVarargs())
        // #define foo(x...)
        Name << "...";

      Name << ')';
    }

    clang::SmallString<128> SpellingBuffer;
    bool First = true;
    for (const auto &T : MI.tokens()) {
      if (!First && T.hasLeadingSpace())
        Value << ' ';

      Value << PP.getSpelling(T, SpellingBuffer);
      First = false;
    }
  }

public:
  IncludeScanner(
      const std::function<build_graph::file_type(std::string_view)> &file_type,
      build_graph::result &r, UniqueIdToNode &id_to_node,
      clang::Preprocessor &pp, const std::filesystem::path &working_dir,
      bool replace_file_optimization)
      : m_r(r), m_sm(&pp.getSourceManager()), m_id_to_node(id_to_node),
        m_file_type(file_type), m_pp(&pp), m_accounted_for_token_count{0u},
        m_working_dir(working_dir),
        m_replace_file_optimization(replace_file_optimization) {}

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

      if (m_stack.empty()) {
        // If our stack's empty, then this is our source file
        auto const [it, inserted] =
            m_id_to_node.emplace(file->getUniqueID(), empty);
        if (inserted) {
          const std::filesystem::path rel =
              std::filesystem::path(file->getName().str())
                  .lexically_relative(m_working_dir);
          it->second.v = add_vertex(rel, m_r.graph);
          it->second.angled_rel = rel.parent_path();
          m_r.sources.push_back(it->second.v);
        } else {
          // TODO: Warn that a source is being included
          assert(false);
        }
        m_stack.push_back(it);
      } else {
        // We should already have added this in `InclusionDirective` or
        // it is a source file that was already added to the graph
        assert(m_id_to_node.count(file->getUniqueID()) > 0);
        m_stack.push_back(m_id_to_node.find(file->getUniqueID()));
      }

      return;
    }
    case FileChangeReason::ExitFile: {
      if (const clang::FileEntry *file =
              m_sm->getFileEntryForID(OptionalPrevFID)) {
        FileState &state = m_stack.back()->second;

        // If we are unguarded, then don't set the 'fully_processed' stuff
        // and move the total cost into the includer.
        const bool guarded =
            m_pp->getHeaderSearchInfo().isFileMultipleIncludeGuarded(file);
        if (guarded) {
          // If we are fully guarded, then make sure that subsequent includes
          // won't do anything.
          if (m_replace_file_optimization) {
            std::string tmp = "#pragma once\n";
            tmp.swap(state.replacement_contents);
            state.replacement_contents += tmp;
          }

          // Apply the costs to ourselves
          apply_costs(file);

          // If we're guarded then we are fully processed and we won't need
          // to enter this file again.
          state.fully_processed = true;

          m_stack.pop_back();
        } else {
          m_r.unguarded_files.insert(m_stack.back()->second.v);

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

    if (!File) {
      // File does not exist
      m_r.missing_includes.emplace(FileName.str());
      return;
    }

    auto const [it, inserted] =
        m_id_to_node.emplace(File->getUniqueID(), empty);

    // If we have seen this file then replace it with the smaller,
    // cheaper file before we attempt to enter it.
    if (it->second.fully_processed && m_replace_file_optimization) {
      m_sm->overrideFileContents(
          File, llvm::MemoryBufferRef(it->second.replacement_contents, ""));
    }

    if (m_stack.back()->second.fully_processed) {
      return;
    }

    const clang::FileID fileID = m_sm->getFileID(HashLoc);
    const char open = IsAngled ? '<' : '"';
    std::string include(&open, 1);
    include.insert(include.cend(), FileName.begin(), FileName.end());
    const char close = IsAngled ? '>' : '"';
    include.insert(include.cend(), &close, &close + 1);
    const Graph::vertex_descriptor from = m_stack.back()->second.v;

    if (m_replace_file_optimization && m_stack.back()->second.fully_processed) {
      m_stack.back()->second.replacement_contents += "#include ";
      m_stack.back()->second.replacement_contents += include;
      m_stack.back()->second.replacement_contents += '\n';
    }

    if (inserted) {
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
          add_vertex(file_node(p)
                         .set_external(clang::SrcMgr::isSystem(FileType))
                         .set_precompiled(is_precompiled),
                     m_r.graph);

      // If we have a non-angled include, then make it relative to the
      // previous path we are storing.
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
    m_r.graph[to].external_incoming += m_r.graph[from].is_external;

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

    // We can get a `nullptr` for `data()`, otherwise our files should
    // always be null-terminated strings and we can access `data()`
    // without a corresponding call to `size()`.
    if (pragma_text.empty()) {
      return;
    }

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
        return;
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
        return;
      }
    }
  }

  void MacroDefined(const clang::Token &MacroNameTok,
                    const clang::MacroDirective *MD) final {
    if (!m_replace_file_optimization) {
      return;
    }

    if (m_stack.back()->second.fully_processed) {
      return;
    }

    // Ignore built-in macros
    if (MD->getMacroInfo()->isBuiltinMacro()) {
      return;
    }

    if (m_sm->isWrittenInBuiltinFile(MD->getLocation()) ||
        m_sm->isWrittenInCommandLineFile(MD->getLocation())) {
      return;
    }

    clang::IdentifierInfo *Id = MacroNameTok.getIdentifierInfo();
    clang::SourceLocation location = MacroNameTok.getLocation();
    std::string NameBuffer, ValueBuffer;
    llvm::raw_string_ostream Name(NameBuffer);
    llvm::raw_string_ostream Value(ValueBuffer);
    writeMacroDefinition(*Id, *MD->getMacroInfo(), *m_pp, Name, Value);

    m_stack.back()->second.replacement_contents += "#define ";
    m_stack.back()->second.replacement_contents += Name.str();
    m_stack.back()->second.replacement_contents += ' ';
    m_stack.back()->second.replacement_contents += Value.str();
    m_stack.back()->second.replacement_contents += '\n';
  }

  void MacroUndefined(const clang::Token &MacroNameTok,
                      const clang::MacroDefinition &MD,
                      const clang::MacroDirective *Undef) final {
    if (!m_replace_file_optimization) {
      return;
    }

    if (m_stack.back()->second.fully_processed) {
      return;
    }

#if 0
	// I don't think we need to handle built-in undefs as
	// `getMacroInfo()` below returns a `nullptr`.
    if (Undef->getMacroInfo()->isBuiltinMacro()) {
      return;
    }
#endif

    // TODO: Need to fully understand and document this return guard
    if (!Undef || m_sm->isWrittenInBuiltinFile(Undef->getLocation()) ||
        m_sm->isWrittenInCommandLineFile(Undef->getLocation())) {
      return;
    }

    m_stack.back()->second.replacement_contents += "#undef ";
    m_stack.back()->second.replacement_contents +=
        MacroNameTok.getIdentifierInfo()->getName();
    m_stack.back()->second.replacement_contents += '\n';
  }

  void EndOfMainFile() final {
    assert(m_stack.size() == 1);
    apply_costs(m_sm->getFileEntryForID(m_sm->getMainFileID()));
  }
};

class ExpensiveAction : public clang::PreprocessOnlyAction {
  clang::ast_matchers::MatchFinder m_f;
  clang::CompilerInstance *m_ci;
  build_graph::result &m_r;
  UniqueIdToNode &m_id_to_node;
  std::function<build_graph::file_type(std::string_view)> m_file_type;
  std::filesystem::path m_working_dir;
  bool m_replace_file_optimization;

public:
  ExpensiveAction(
      build_graph::result &r, UniqueIdToNode &id_to_node,
      const std::function<build_graph::file_type(std::string_view)> &file_type,
      const std::filesystem::path &working_dir, bool replace_file_optimization)
      : m_f(), m_ci(nullptr), m_r(r), m_id_to_node(id_to_node),
        m_file_type(file_type), m_working_dir(working_dir),
        m_replace_file_optimization(replace_file_optimization) {}

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
        std::make_unique<IncludeScanner>(m_file_type, m_r, m_id_to_node,
                                         m_ci->getPreprocessor(), m_working_dir,
                                         m_replace_file_optimization));

    clang::PreprocessOnlyAction::ExecuteAction();
  }
};

/// This component is a concrete implementation of a `FrontEndActionFactory`
/// that will output the include directives along with the total file size
/// that would be saved if it was deleted.
class find_graph_factory : public clang::tooling::FrontendActionFactory {
  build_graph::result &m_r;
  UniqueIdToNode &m_id_to_node;
  std::function<build_graph::file_type(std::string_view)> m_file_type;
  std::filesystem::path m_working_dir;
  bool m_replace_file_optimization;

public:
  /// Create a `print_graph_factory`.
  find_graph_factory(
      build_graph::result &r, UniqueIdToNode &id_to_node,
      const std::function<build_graph::file_type(std::string_view)> &file_type,
      const std::filesystem::path &working_dir, bool replace_file_optimization)
      : m_r(r), m_id_to_node(id_to_node), m_working_dir(working_dir),
        m_file_type(file_type),
        m_replace_file_optimization(replace_file_optimization) {}

  /// Returns a new `clang::FrontendAction`.
  std::unique_ptr<clang::FrontendAction> create() final {
    return std::make_unique<ExpensiveAction>(m_r, m_id_to_node, m_file_type,
                                             m_working_dir,
                                             m_replace_file_optimization);
  }
};

} // namespace

llvm::Expected<build_graph::result> build_graph::from_compilation_db(
    const clang::tooling::CompilationDatabase &compilation_db,
    const std::filesystem::path &working_dir,
    std::span<const std::filesystem::path> source_paths,
    std::function<build_graph::file_type(std::string_view)> file_type,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs, options opts) {
  std::vector<std::string> source_path_strings(source_paths.size());
  std::transform(source_paths.begin(), source_paths.end(),
                 source_path_strings.begin(),
                 [](const std::filesystem::path &p) { return p.string(); });

  clang::tooling::ClangTool tool(
      compilation_db, source_path_strings,
      std::make_shared<clang::PCHContainerOperations>(), fs);

  UniqueIdToNode id_to_node;
  result r;
  find_graph_factory f(r, id_to_node, file_type, working_dir,
                       opts.replace_file_optimization);
  const int rc = tool.run(&f);
  if (rc != 0) {
    return llvm::createStringError(std::error_code(rc, std::generic_category()),
                                   "oops");
  }
  return r;
}

llvm::Expected<build_graph::result> build_graph::from_compilation_db(
    const clang::tooling::CompilationDatabase &compilation_db,
    const std::filesystem::path &working_dir,
    std::initializer_list<const std::filesystem::path> source_paths,
    std::function<build_graph::file_type(std::string_view)> file_type,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs, options opts) {
  return from_compilation_db(
      compilation_db, working_dir,
      std::span(source_paths.begin(), source_paths.end()), file_type, fs, opts);
}

llvm::Expected<build_graph::result> build_graph::from_dir(
    std::filesystem::path source_dir,
    std::span<const std::pair<std::filesystem::path,
                              clang::SrcMgr::CharacteristicKind>>
        include_dirs,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
    std::function<file_type(std::string_view)> file_type, options opts,
    std::span<const std::filesystem::path> forced_includes) {
  source_dir = std::filesystem::absolute(source_dir);
  assert(std::all_of(include_dirs.begin(), include_dirs.end(),
                     [](const auto &p) { return p.first.is_absolute(); }));

  FakeCompilationDatabase db(source_dir);
  db.m_include_dirs = include_dirs;
  db.m_force_includes = forced_includes;

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
        db.m_sources.emplace_back(it->path().str());
      }
      it.increment(ec);
    }
  }

  return from_compilation_db(db, source_dir, db.m_sources, file_type, fs, opts);
}

llvm::Expected<build_graph::result> build_graph::from_dir(
    const std::filesystem::path &source_dir,
    std::initializer_list<
        std::pair<std::filesystem::path, clang::SrcMgr::CharacteristicKind>>
        include_dirs,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
    std::function<file_type(std::string_view)> file_type, options opts,
    std::initializer_list<std::filesystem::path> forced_includes) {
  return from_dir(source_dir,
                  std::span(include_dirs.begin(), include_dirs.end()), fs,
                  file_type, opts,
                  std::span(forced_includes.begin(), forced_includes.end()));
}

std::ostream &operator<<(std::ostream &out, build_graph::options opts) {
  return out << "options(replace_file_optimization=" << std::boolalpha
             << opts.replace_file_optimization << ")";
}

} // namespace IncludeGuardian
