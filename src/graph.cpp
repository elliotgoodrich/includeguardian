#include "graph.hpp"

#include <ostream>

namespace IncludeGuardian {

std::ostream &operator<<(std::ostream &stream, const file_node &value) {
  return stream << value.path << " (" << value << ")";
}

bool operator==(const file_node &lhs, const file_node &rhs) {
  return lhs.file_size == rhs.file_size && lhs.path == rhs.path;
}

bool operator!=(const file_node &lhs, const file_node &rhs) {
  return !(lhs == rhs);
}

std::ostream &operator<<(std::ostream &stream, const include_edge &value) {
  return stream << value.code << "#" << value.lineNumber;
}

bool operator==(const include_edge &lhs, const include_edge &rhs) {
  return lhs.code == rhs.code && lhs.lineNumber == rhs.lineNumber;
}

bool operator!=(const include_edge &lhs, const include_edge &rhs) {
  return !(lhs == rhs);
}

} // namespace IncludeGuardian
