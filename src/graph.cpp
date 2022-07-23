#include "graph.hpp"

#include <boost/units/io.hpp>

#include <ostream>

namespace IncludeGuardian {

std::ostream &operator<<(std::ostream &stream, const file_node &value) {
  return stream << value.path << ' ' << value.cost
                << " [incoming=" << value.internal_incoming << ']'
                << (value.is_external ? " [external]" : "")
                << (value.component ? " [linked]" : "");
}

std::ostream &operator<<(std::ostream &stream, const include_edge &value) {
  return stream << value.code << "#" << value.lineNumber
                << (value.is_removable ? "" : " not removable");
}

bool operator==(const include_edge &lhs, const include_edge &rhs) {
  return lhs.code == rhs.code && lhs.lineNumber == rhs.lineNumber &&
         lhs.is_removable == rhs.is_removable;
}

bool operator!=(const include_edge &lhs, const include_edge &rhs) {
  return !(lhs == rhs);
}

} // namespace IncludeGuardian
