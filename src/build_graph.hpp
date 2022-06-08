#ifndef INCLUDE_GUARD_834474B2_99C7_440E_8576_7A89EE4F086A
#define INCLUDE_GUARD_834474B2_99C7_440E_8576_7A89EE4F086A

#include "graph.hpp"

#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/Support/Error.h>

#include <span>
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
    std::vector<std::string> missing_files;
  };
  static llvm::Expected<result>
  from_compilation_db(const clang::tooling::CompilationDatabase &compilation_db,
                      std::span<const std::string> source_paths,
                      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs);
};

} // namespace IncludeGuardian

#endif