#ifndef INCLUDE_GUARD_834474B2_99C7_440E_8576_7A89EE4F086A
#define INCLUDE_GUARD_834474B2_99C7_440E_8576_7A89EE4F086A

#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/Support/Error.h>

#include <boost/graph/adjacency_list.hpp>

#include <span>
#include <utility>
#include <vector>

namespace clang::tooling { class CompilationDatabase; }
namespace llvm::vfs { class FileSystem; }

namespace IncludeGuardian {

class file_node {
public:
  std::string path;
  std::size_t fileSizeInBytes = 0;
};

class include_edge {
public:
  std::string code;
};

using Graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
                                    file_node, include_edge>;

struct build_graph {
  static llvm::Expected<std::pair<Graph, std::vector<Graph::vertex_descriptor>>>
  from_compilation_db(const clang::tooling::CompilationDatabase &compilation_db,
                      std::span<const std::string> source_paths,
                      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs);
};

} // namespace IncludeGuardian

#endif