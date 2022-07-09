#include "graph.hpp"

#include <boost/units/io.hpp>

#include <ostream>

namespace IncludeGuardian {

std::ostream &operator<<(std::ostream &stream, const file_node &value) {
  return stream << value.path << ' ' << value.cost << ' '
                << (value.is_external ? " external" : "");
}

bool operator==(const file_node &lhs, const file_node &rhs) {
  return lhs.path == rhs.path && lhs.is_external == rhs.is_external &&
         lhs.cost == rhs.cost;
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
