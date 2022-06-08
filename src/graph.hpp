#ifndef INCLUDE_GUARD_9228E240_D576_4608_9B9A_9D747F5AEF27
#define INCLUDE_GUARD_9228E240_D576_4608_9B9A_9D747F5AEF27

#include <boost/graph/adjacency_list.hpp>

#include <iosfwd>
#include <string>

namespace IncludeGuardian {

class file_node {
public:
  std::string path;
  std::size_t fileSizeInBytes = 0;
};

std::ostream &operator<<(std::ostream &stream, const file_node &value);
bool operator==(const file_node &lhs, const file_node &rhs);
bool operator!=(const file_node &lhs, const file_node &rhs);

class include_edge {
public:
  std::string code;
  unsigned lineNumber;
};

std::ostream &operator<<(std::ostream &stream, const include_edge &value);
bool operator==(const include_edge &lhs, const include_edge &rhs);
bool operator!=(const include_edge &lhs, const include_edge &rhs);

using Graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
                                    file_node, include_edge>;

} // namespace IncludeGuardian

#endif