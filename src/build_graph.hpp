#ifndef INCLUDE_GUARD_834474B2_99C7_440E_8576_7A89EE4F086A
#define INCLUDE_GUARD_834474B2_99C7_440E_8576_7A89EE4F086A

#include "graph.hpp"

#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/Support/Error.h>

#include <filesystem>
#include <functional>
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
    std::vector<std::filesystem::path> missing_files;
  };

  enum class file_type {
    source,
    header,
    ignore,
  };

  static llvm::Expected<result>
  from_compilation_db(const clang::tooling::CompilationDatabase &compilation_db,
                      std::span<const std::filesystem::path> source_paths,
                      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs);

  static llvm::Expected<result>
  from_dir(const llvm::Twine &dir, std::span<const std::filesystem::path> include_dirs,
           llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
           std::function<file_type(std::string_view)> file_type);
};

} // namespace IncludeGuardian

#endif