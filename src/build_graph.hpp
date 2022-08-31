#ifndef INCLUDE_GUARD_834474B2_99C7_440E_8576_7A89EE4F086A
#define INCLUDE_GUARD_834474B2_99C7_440E_8576_7A89EE4F086A

#include "graph.hpp"

#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/Support/Error.h>

#include <filesystem>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clang::tooling {
class CompilationDatabase;
}
namespace llvm::vfs {
class FileSystem;
}

namespace IncludeGuardian {

struct build_graph {
  struct result {
    Graph graph;
    std::vector<Graph::vertex_descriptor> sources;
    std::set<std::string> missing_includes;
  };

  enum class file_type {
    source,
    header,
    precompiled_header,
    ignore,
  };

  static llvm::Expected<result>
  from_compilation_db(const clang::tooling::CompilationDatabase &compilation_db,
                      const std::filesystem::path& working_dir,
                      std::span<const std::filesystem::path> source_paths,
                      std::function<file_type(std::string_view)> file_type,
                      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs);
  static llvm::Expected<result>
  from_compilation_db(const clang::tooling::CompilationDatabase &compilation_db,
                      const std::filesystem::path& working_dir,
                      std::initializer_list<const std::filesystem::path> source_paths,
                      std::function<file_type(std::string_view)> file_type,
                      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs);

  // Try to construct a `Graph` object from all files in the specified
  // `source_dir` that return `source` from `file_type` (and files they
  // include).  Use `include_dirs` to specify additional directories in which to
  // find files and use the specified `fs` to perform all file actions.  If
  // `common_header` is set, then inject this as an include at the beginning of
  // all source files.  This will commonly be used for precompiled headers.
  static llvm::Expected<result>
  from_dir(std::filesystem::path source_dir,
           std::span<const std::filesystem::path> include_dirs,
           llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
           std::function<file_type(std::string_view)> file_type,
           std::span<const std::filesystem::path> forced_includes =
               std::span<const std::filesystem::path>());
  static llvm::Expected<result>
  from_dir(const std::filesystem::path &source_dir,
           std::initializer_list<std::filesystem::path> include_dirs,
           llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
           std::function<file_type(std::string_view)> file_type,
           std::initializer_list<std::filesystem::path> forced_includes = {});
};

} // namespace IncludeGuardian

#endif