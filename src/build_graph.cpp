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

#include <llvm/ADT/ScopeExit.h>
#include <llvm/ADT/Triple.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <numeric>
#include <string>
#include <utility>

namespace IncludeGuardian {

namespace {

struct Hasher {
  std::size_t operator()(const llvm::sys::fs::UniqueID key) const noexcept {
    // NOTE: Not a good hash, but our devices should probably be the same
    return key.getFile() ^ key.getDevice();
  }
};

const bool LOG = false;

// This function gives us a way to restrict ourselves to a subset of
// includes in order to debug more easily.  To enable, remove the
// `return true` and add filenames to the `allow_list`.
bool allowed(const clang::FileEntry *f) {
    return true;
    const std::initializer_list<llvm::StringRef> allow_list = {};

  return std::find_if(allow_list.begin(), allow_list.end(), [f](auto x) {
           return x ==
                  std::filesystem::path(f->getName().str()).filename().string();
         }) != allow_list.end();
}

/* This component is needed on Windows because if we include a file
   from using both <> and "" we will get a backslash or forward slash
   respectively between the path and the filename.  This is because of
   clangs `HeaderSearch.cpp`

      // Concatenate the requested file onto the directory.
      // FIXME: Portability.  Filename concatenation should be in sys::Path.
      TmpDir = IncluderAndDir.second->getName();
      TmpDir.push_back('/');
      TmpDir.append(Filename.begin(), Filename.end());

   Windows is also case insensitive so we may have multiple different paths
   refer to the same file.

   This `FileSystem` allows us to overwrite a file in the underlying
   `FileSystem` by `UniqueID`, which means that we continue to use the
   underlying `FileSystem` to decide what paths map to which path.
*/
class OverwriteFileSystem : public llvm::vfs::FileSystem {

  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> m_underlying;
  llvm::vfs::InMemoryFileSystem m_overwrites;
  std::unordered_map<llvm::sys::fs::UniqueID, std::string, Hasher> m_lookup;

public:
  explicit OverwriteFileSystem(
      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> underlying)
      : m_underlying(std::move(underlying)), m_overwrites(), m_lookup() {}

  llvm::sys::fs::UniqueID replace(const llvm::Twine &path,
                                  llvm::sys::fs::UniqueID id,
                                  std::unique_ptr<llvm::MemoryBuffer> buffer) {
    assert(m_lookup.count(id) == 0);
    [[maybe_unused]] const bool inserted =
        m_overwrites.addFile(path, 0, std::move(buffer));
    assert(inserted);
    m_lookup.emplace(id, path.str());
    return m_overwrites.status(path)->getUniqueID();
  }

  llvm::ErrorOr<llvm::vfs::Status> status(const llvm::Twine &path) final {
    llvm::ErrorOr<llvm::vfs::Status> s = m_underlying->status(path);
    if (!s) {
      return s;
    }

    const auto it = m_lookup.find(s->getUniqueID());
    if (it != m_lookup.end()) {
      return m_overwrites.status(it->second);
    }

    return s;
  }

  llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>>
  openFileForRead(const llvm::Twine &path) final {
    llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>> f =
        m_underlying->openFileForRead(path);
    if (!f) {
      return f;
    }

    if (f.get() == nullptr) {
      return f;
    }

    const llvm::ErrorOr<llvm::vfs::Status> &s = f->get()->status();
    if (!s) {
      return f;
    }

    const auto it = m_lookup.find(s->getUniqueID());
    if (it != m_lookup.end()) {
      return m_overwrites.openFileForRead(it->second);
    }

    return f;
  }

  llvm::vfs::directory_iterator dir_begin(const llvm::Twine &Dir,
                                          std::error_code &EC) final {
    return m_underlying->dir_begin(Dir, EC);
  }
  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const final {
    return m_underlying->getCurrentWorkingDirectory();
  }
  std::error_code setCurrentWorkingDirectory(const llvm::Twine &Path) final {
    return m_underlying->setCurrentWorkingDirectory(Path);
  }
  std::error_code getRealPath(const llvm::Twine &Path,
                              llvm::SmallVectorImpl<char> &Output) const final {
    return m_underlying->getRealPath(Path, Output);
  }
  std::error_code isLocal(const llvm::Twine &Path, bool &Result) final {
    return m_underlying->isLocal(Path, Result);
  }

protected:
  FileSystem &getUnderlyingFS() { return *m_underlying; }

  virtual void anchor() {}
};

class LoggingFileSystem : public llvm::vfs::FileSystem {
public:
  explicit LoggingFileSystem(llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS)
      : FS(std::move(FS)) {}

  llvm::ErrorOr<llvm::vfs::Status> status(const llvm::Twine &Path) final {
    llvm::ErrorOr<llvm::vfs::Status> s = FS->status(Path);
    if (s) {
      llvm::SmallVector<char, 256> out;
      std::cout << "status(" << Path.toStringRef(out).str()
                << ") = " << s.get().getUniqueID().getFile() << "\n";
    }
    return s;
  }
  llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>>
  openFileForRead(const llvm::Twine &Path) final {
    llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>> f =
        FS->openFileForRead(Path);
    if (f) {
      llvm::SmallVector<char, 256> out;
      std::cout << "openFileForRead(" << Path.toStringRef(out).str()
                << ") = " << f.get()->status()->getUniqueID().getFile() << "\n";
    }
    return f;
  }
  llvm::vfs::directory_iterator dir_begin(const llvm::Twine &Dir,
                                          std::error_code &EC) final {
    return FS->dir_begin(Dir, EC);
  }
  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const final {
    return FS->getCurrentWorkingDirectory();
  }
  std::error_code setCurrentWorkingDirectory(const llvm::Twine &Path) final {
    return FS->setCurrentWorkingDirectory(Path);
  }
  std::error_code getRealPath(const llvm::Twine &Path,
                              llvm::SmallVectorImpl<char> &Output) const final {
    return FS->getRealPath(Path, Output);
  }
  std::error_code isLocal(const llvm::Twine &Path, bool &Result) final {
    return FS->isLocal(Path, Result);
  }

protected:
  FileSystem &getUnderlyingFS() { return *FS; }

private:
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS;

  virtual void anchor() {}
};

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

struct FileState {
  Graph::vertex_descriptor v;
#ifdef _DEBUG
  std::string debug_name;
#endif
  std::filesystem::path angled_rel; //< This is the relative path of `v`
                                    //< compared to the last angled include seen
  bool fully_processed = false;
  // A file becomes fully processed once it has been exited and the
  // corresponding entry in the `Graph` is complete.
  std::string replacement_contents =
      "#pragma once\n"; //< If we are `fully_processed` this will
                        //< contain a C++ file that is equivalent
                        //< when this is included by another file
                        //< (i.e. only the preprocessor definitions)
};

using UniqueIdToNode =
    std::unordered_map<llvm::sys::fs::UniqueID, FileState, Hasher>;

struct InProgress {
  UniqueIdToNode::iterator it;
  cost c;
  std::optional<std::uint64_t> overridden_token_count;
  std::optional<boost::units::quantity<boost::units::information::info>>
      overridden_file_size;

  InProgress(UniqueIdToNode::iterator it) : it(it) {}
};

struct ReplaceWith {
  std::unique_ptr<llvm::MemoryBuffer> contents;
  std::string path;
  Graph::vertex_descriptor v;
};

using NeedsReplacing = std::unordered_map<llvm::sys::fs::UniqueID, ReplaceWith, Hasher>;

struct IncludeScanner : public clang::PPCallbacks {
  clang::SourceManager *m_sm;
  UniqueIdToNode &m_id_to_node;
  NeedsReplacing &m_needs_replacing;
  std::vector<bool> &m_replaced;
  build_graph::result &m_r;
  std::function<build_graph::file_type(std::string_view)> m_file_type;
  std::vector<InProgress> m_stack;
  std::uint64_t m_accounted_for_token_count;
  clang::Preprocessor *m_pp;
  std::filesystem::path m_working_dir;
  bool m_replace_file_optimization;
  int m_skip_count = 0;

  void update_cost_when_leaving_file(const clang::FileEntry *file) {
    InProgress &p = m_stack.back();
    if (p.overridden_file_size) {
      p.c.file_size = *p.overridden_file_size;
    } else {
      p.c.file_size += file->getSize() * boost::units::information::bytes;
    }

    const std::uint64_t token_count = m_pp->getTokenCount();
    if (p.overridden_token_count) {
      p.c.token_count = *p.overridden_token_count;
    } else {
      p.c.token_count += token_count - m_accounted_for_token_count;
    }
    m_accounted_for_token_count = token_count;
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
      NeedsReplacing &needs_replacing, std::vector<bool> &replaced,
      clang::Preprocessor &pp, const std::filesystem::path &working_dir,
      bool replace_file_optimization)
      : m_r(r), m_sm(&pp.getSourceManager()), m_id_to_node(id_to_node),
        m_needs_replacing(needs_replacing), m_replaced(replaced),
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
        auto const [it, inserted] =
            m_id_to_node.emplace(file->getUniqueID(), empty);

        // TODO: Warn that a source is being included
        // If our stack's empty, then this is our source file
        assert(inserted);
        const std::filesystem::path rel =
            std::filesystem::path(file->getName().str())
                .lexically_relative(m_working_dir);
        it->second.v = add_vertex(rel, m_r.graph);
        if (m_replace_file_optimization) {
          m_replaced.resize(it->second.v + 1);
        }
#ifdef _DEBUG
        it->second.debug_name = file->getName();
#endif
        it->second.angled_rel = rel.parent_path();

        m_r.sources.push_back(it->second.v);
        m_stack.emplace_back(it);

        // Check that if we are looking at our source that we haven't got
        // any unaccounted for tokens somehow
        assert(m_pp->getTokenCount() == m_accounted_for_token_count);
      } else {
        if (!allowed(file)) {
          ++m_skip_count;
          return;
        }

        // Assign all lexed tokens to the file before we enter this
        // new one
        const std::uint64_t token_count = m_pp->getTokenCount();
        m_stack.back().c.token_count =
            token_count - m_accounted_for_token_count;
        m_accounted_for_token_count = token_count;

        // We should already have added this in `InclusionDirective` or
        // it is a source file that was already added to the graph
        assert(m_id_to_node.count(file->getUniqueID()) > 0);

        // We can get here if we have a guarded file that included different
        // files depending on defines.  This is most likely an issue and a poor
        // design of files. TODO: Warn on this!
        assert(m_id_to_node.find(file->getUniqueID())->second.v != empty);
        m_stack.emplace_back(m_id_to_node.find(file->getUniqueID()));
      }

      return;
    }
    case FileChangeReason::ExitFile: {
      const clang::FileEntry *file = m_sm->getFileEntryForID(OptionalPrevFID);
      if (!file) {
        // Ignore if this is the predefines
        return;
      }

      if (!allowed(file)) {
        --m_skip_count;
        return;
      }

      // Update `InProgress` from the costs of `file`.
      update_cost_when_leaving_file(file);

      InProgress &p = m_stack.back();
      FileState &state = p.it->second;

      // If we are unguarded then move the total cost into the includer.
      const bool guarded =
          m_pp->getHeaderSearchInfo().isFileMultipleIncludeGuarded(file);
      if (!guarded) {
        m_r.unguarded_files.insert(state.v);

        // If we're not guarded then push the cost to our includer
        const cost x = p.c;
        m_stack.pop_back();
        m_stack.back().c += x;

        state.fully_processed = true;
      } else {
        file_node &node = m_r.graph[state.v];
        if (!state.fully_processed) {
          // If we are fully guarded, then make sure that subsequent includes
          // won't do anything.
          if (m_replace_file_optimization && !m_replaced[state.v]) {
            m_needs_replacing.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(file->getUniqueID()),
                std::forward_as_tuple(llvm::MemoryBuffer::getMemBufferCopy(
                                          state.replacement_contents, ""),
                                      file->tryGetRealPathName().str(),
                                      state.v));
          }

          // Mark it at guarded
          node.set_guarded(true);

          // Apply the costs to ourselves and double check we haven't
          // done it yet
          assert(node.underlying_cost == cost{});
          node.underlying_cost = p.c;

          // If we're guarded then we are fully processed and we won't need
          // to enter this file again.
          state.fully_processed = true;
        }

        assert(guarded && state.fully_processed);
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

    if (!File) {
      // File does not exist
      m_r.missing_includes.emplace(FileName.str());
      return;
    }

    if (m_skip_count) {
      return;
    }

    if (!allowed(File)) {
      return;
    }

    auto const [it, inserted] =
        m_id_to_node.emplace(File->getUniqueID(), empty);

    FileState &state = m_stack.back().it->second;
    if (state.fully_processed && m_r.graph[state.v].is_guarded) {
      // We can avoid doing any processing here if we have already seen
      // this file and it is unguarded.  For unguarded files (like X macros),
      // they may conditionally include other files depending on what is defined
      // at the point they are defined, and this may change each time it's
      // included
      assert(!inserted);
      return;
    }

    if (inserted) {
      // If we don't have an angled include, try and build up the relative
      // path from the first angled include.
      const std::filesystem::path p =
          (IsAngled ? std::filesystem::path(RelativePath.str())
                    : state.angled_rel / RelativePath.str())
              .make_preferred()
              .lexically_normal();

      // We are also precompiled if the header including us is
      // precompiled
      const bool is_precompiled =
          (!m_stack.empty() && m_r.graph[state.v].is_precompiled) ||
          m_file_type(p.string()) == build_graph::file_type::precompiled_header;

      it->second.v =
          add_vertex(file_node(p)
                         .set_external(clang::SrcMgr::isSystem(FileType))
                         .set_precompiled(is_precompiled),
                     m_r.graph);
      if (m_replace_file_optimization) {
        m_replaced.resize(it->second.v + 1);
      }
#ifdef _DEBUG
      it->second.debug_name = RelativePath.str();
#endif

      // If we have a non-angled include, then make it relative to the
      // previous path we are storing.
      const std::filesystem::path relative_path(RelativePath.str());
      it->second.angled_rel =
          (IsAngled ? relative_path : state.angled_rel / relative_path)
              .parent_path();
    }

    const Graph::vertex_descriptor from = state.v;
    const Graph::vertex_descriptor to = it->second.v;

    // If we already have this edge, then skip everything below.  This can
    // happen for unguarded files as we do not exit early above because the
    // set of includes may vary each time it is included.
    if (edge(from, to, m_r.graph).second) {
      return;
    }

    const clang::FileID fileID = m_sm->getFileID(HashLoc);
    const char open = IsAngled ? '<' : '"';
    std::string include(&open, 1);
    include.insert(include.cend(), FileName.begin(), FileName.end());
    const char close = IsAngled ? '>' : '"';
    include.insert(include.cend(), &close, &close + 1);

    if (m_replace_file_optimization && m_replaced[state.v]) {
      state.replacement_contents += "#include ";
      state.replacement_contents += include;
      state.replacement_contents += '\n';
    }

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
    if (m_skip_count) {
      return;
    }
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
          m_stack.back().overridden_file_size =
              file_size * boost::units::information::bytes;
        }
        return;
      }
    }

    {
      const clang::StringRef prefix = "#pragma override_token_count(";
      if (std::equal(prefix.begin(), prefix.end(), pragma_text.data())) {
        std::uint64_t token_count;
        const char *start = pragma_text.data() + prefix.size();
        const char *end = std::strchr(start, ')');
        const auto [ptr, ec] = std::from_chars(start, end, token_count);
        if (ec == std::errc()) {
          m_stack.back().overridden_token_count = token_count;
        }
        return;
      }
    }
  }

  void MacroDefined(const clang::Token &MacroNameTok,
                    const clang::MacroDirective *MD) final {
    if (m_skip_count) {
      return;
    }
    if (!m_replace_file_optimization) {
      return;
    }

    FileState &state = m_stack.back().it->second;
    if (state.fully_processed || m_replaced[state.v]) {
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

    state.replacement_contents += "#define ";
    state.replacement_contents += Name.str();
    state.replacement_contents += ' ';
    state.replacement_contents += Value.str();
    state.replacement_contents += '\n';
  }

  void MacroUndefined(const clang::Token &MacroNameTok,
                      const clang::MacroDefinition &MD,
                      const clang::MacroDirective *Undef) final {
    if (m_skip_count) {
      return;
    }
    if (!m_replace_file_optimization) {
      return;
    }

    FileState &state = m_stack.back().it->second;
    if (state.fully_processed || m_replaced[state.v]) {
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

    state.replacement_contents += "#undef ";
    state.replacement_contents += MacroNameTok.getIdentifierInfo()->getName();
    state.replacement_contents += '\n';
  }

  void EndOfMainFile() final {
    assert(m_stack.size() == 1);
    update_cost_when_leaving_file(
        m_sm->getFileEntryForID(m_sm->getMainFileID()));
    m_r.graph[m_stack.back().it->second.v].underlying_cost = m_stack.back().c;
  }
};

class ExpensiveAction : public clang::PreprocessOnlyAction {
  clang::ast_matchers::MatchFinder m_f;
  clang::CompilerInstance *m_ci;
  build_graph::result &m_r;
  UniqueIdToNode &m_id_to_node;
  NeedsReplacing m_needs_replacing;
  std::vector<bool> &m_replaced; // vertex_descriptor -> isReplaced
  llvm::IntrusiveRefCntPtr<OverwriteFileSystem> m_in_memory_fs;
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> m_fs;
  std::function<build_graph::file_type(std::string_view)> m_file_type;
  std::filesystem::path m_working_dir;

public:
  ExpensiveAction(
      build_graph::result &r, UniqueIdToNode &id_to_node,
      std::vector<bool> &replaced,
      llvm::IntrusiveRefCntPtr<OverwriteFileSystem> in_memory_fs,
      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
      const std::function<build_graph::file_type(std::string_view)> &file_type,
      const std::filesystem::path &working_dir, bool replace_file_optimization)
      : m_f(), m_ci(nullptr), m_r(r), m_id_to_node(id_to_node),
        m_needs_replacing(), m_replaced(replaced), m_in_memory_fs(in_memory_fs),
        m_fs(fs), m_file_type(file_type), m_working_dir(working_dir) {}

  bool BeginInvocation(clang::CompilerInstance &ci) final {
    ci.getDiagnostics().setSuppressAllDiagnostics(true);
    ci.getDiagnostics().setSuppressSystemWarnings(true);
    ci.getDiagnostics().setIgnoreAllWarnings(true);
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
            m_file_type, m_r, m_id_to_node, m_needs_replacing, m_replaced,
            m_ci->getPreprocessor(), m_working_dir, m_in_memory_fs != nullptr));

    clang::PreprocessOnlyAction::ExecuteAction();
  }

  void EndSourceFileAction() final {
    for (auto &[id, value] : m_needs_replacing) {
      const llvm::sys::fs::UniqueID new_id =
          m_in_memory_fs->replace(value.path, id, std::move(value.contents));
      assert(id != new_id); // Should never happen
      m_replaced[value.v] = true;

      // Since we're overriding our file, it will get a new `UniqueID` and we
      // should replace it in the `UniqueID` lookup to the new one
      const auto prev = m_id_to_node.find(id);
      const auto [it, inserted] = m_id_to_node.emplace(new_id, prev->second);
      assert(inserted);
      m_id_to_node.erase(prev);
    }
    m_needs_replacing.clear();
  }
};

/// This component is a concrete implementation of a `FrontEndActionFactory`
/// that will output the include directives along with the total file size
/// that would be saved if it was deleted.
class find_graph_factory : public clang::tooling::FrontendActionFactory {
  build_graph::result &m_r;
  UniqueIdToNode &m_id_to_node;
  std::vector<bool> m_replaced; // vertex_descriptor -> isReplaced
  llvm::IntrusiveRefCntPtr<OverwriteFileSystem> m_in_memory_fs;
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> m_fs;
  std::function<build_graph::file_type(std::string_view)> m_file_type;
  std::filesystem::path m_working_dir;

public:
  /// Create a `print_graph_factory`.
  find_graph_factory(
      build_graph::result &r, UniqueIdToNode &id_to_node,
      llvm::IntrusiveRefCntPtr<OverwriteFileSystem> in_memory_fs,
      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
      const std::function<build_graph::file_type(std::string_view)> &file_type,
      const std::filesystem::path &working_dir)
      : m_r(r), m_id_to_node(id_to_node), m_replaced(),
        m_in_memory_fs(in_memory_fs), m_fs(fs), m_working_dir(working_dir),
        m_file_type(file_type) {}

  /// Invokes the compiler with a FrontendAction created by create().
  bool
  runInvocation(std::shared_ptr<clang::CompilerInvocation> Invocation,
                clang::FileManager *Files,
                std::shared_ptr<clang::PCHContainerOperations> PCHContainerOps,
                clang::DiagnosticConsumer *DiagConsumer) final {
    if (m_in_memory_fs) {
      // For performance, `ClangTool` shares the `clang::FileManager *` across
      // all translation units.  This FileManager has a cache of file paths
      // against their `File *`, entries which stops us being able to swap out
      // our files in the VFS.
      //
      // So here we override `runInvocation`, disregard the `FileManager *`
      // passed in here and call the base `runInvocation` with a completely new
      // `FileManager`. This allows us to swap out the files in the VFS and they
      // will be looked up again for each new translation unit - at an extra
      // (but comparatively lower) cost.
      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fake_fs(
          &Files->getVirtualFileSystem());
      // Even though the interface takes a `FileManager *`, it expects an
      // `IntrusiveRefCntPtr<FileManager>`
      auto new_file_mgr = llvm::makeIntrusiveRefCnt<clang::FileManager>(
          Files->getFileSystemOpts(), fake_fs);
      return clang::tooling::FrontendActionFactory::runInvocation(
          std::move(Invocation), new_file_mgr.get(), std::move(PCHContainerOps),
          DiagConsumer);
    } else {
      return clang::tooling::FrontendActionFactory::runInvocation(
          std::move(Invocation), Files, std::move(PCHContainerOps),
          DiagConsumer);
    }
  }

  /// Returns a new `clang::FrontendAction`.
  std::unique_ptr<clang::FrontendAction> create() final {
    return std::make_unique<ExpensiveAction>(
        m_r, m_id_to_node, m_replaced, m_in_memory_fs, m_fs, m_file_type,
        m_working_dir, m_in_memory_fs != nullptr);
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

  // Avoid re-preprocessing files that we have seen before and already
  // added to the graph.  By using `OverwriteFileSystem` we can,
  //
  //   1. Keep track of the replacement contents, but put them in a map
  //      of UniqueID against contents
  //   2. Whenever we call `ExecuteAction` (meaning we have a completely
  //      new source file we're looking at) we load all the map contents
  //      into our `OverwriteFileSystem`.  Hopefully at this point we can
  //      guarantee that there is no state left over from the previous
  //      source files.
  //   3. In the future, figure out how we can do this while processing
  //      sources in parallel.

  llvm::IntrusiveRefCntPtr<OverwriteFileSystem> in_memory;
  if (opts.replace_file_optimization) {
    fs = (in_memory = llvm::makeIntrusiveRefCnt<OverwriteFileSystem>(fs));
  }

  if (LOG) {
    fs = llvm::makeIntrusiveRefCnt<LoggingFileSystem>(fs);
  }

  clang::tooling::ClangTool tool(
      compilation_db, source_path_strings,
      std::make_shared<clang::PCHContainerOperations>(), fs);

  // Set our diagnosic consumer here as we get some diagnostics emitted before
  // `BeginInvocation` is called, e.g.
  //   > warning: treating 'c' input as 'c++' when in C++ mode, this behavior
  //   > is deprecated [-Wdeprecated]
  tool.setDiagnosticConsumer(&s_ignore);

  UniqueIdToNode id_to_node;
  result r;
  find_graph_factory f(r, id_to_node, in_memory, fs, file_type, working_dir);
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
