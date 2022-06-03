#ifndef INCLUDE_GUARD_9228E240_D576_4608_9B9A_9D747F5AEF27
#define INCLUDE_GUARD_9228E240_D576_4608_9B9A_9D747F5AEF27

#include <boost/graph/adjacency_list.hpp>

#include <string>

namespace IncludeGuardian {

class file_node {
public:
  std::string path;
  std::size_t fileSizeInBytes = 0;
};

class include_edge {
public:
  std::string code;
  unsigned lineNumber = 0;
};

using Graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
                                    file_node, include_edge>;

} // namespace IncludeGuardian

#endif