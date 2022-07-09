#ifndef INCLUDE_GUARD_9228E240_D576_4608_9B9A_9D747F5AEF27
#define INCLUDE_GUARD_9228E240_D576_4608_9B9A_9D747F5AEF27

#include "cost.hpp"

#include <boost/graph/adjacency_list.hpp>

#include <boost/units/quantity.hpp>
#include <boost/units/systems/information/byte.hpp>

#include <filesystem>
#include <iosfwd>
#include <string>

namespace IncludeGuardian {

class file_node {
public:
  std::filesystem::path path; //< Note that this will most likely be
                              //< a relative path (e.g. boost/foo.hpp) and
                              //< it will be unknown and generally unnecessary
                              //< as to what path it is relative to.
  bool is_external = false; //< Whether this file comes from an external library
  cost cost;
};

std::ostream &operator<<(std::ostream &stream, const file_node &value);
bool operator==(const file_node &lhs, const file_node &rhs);
bool operator!=(const file_node &lhs, const file_node &rhs);

class include_edge {
public:
  std::string code;
  unsigned lineNumber;
  bool is_removable = true;
};

std::ostream &operator<<(std::ostream &stream, const include_edge &value);
bool operator==(const include_edge &lhs, const include_edge &rhs);
bool operator!=(const include_edge &lhs, const include_edge &rhs);

using Graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
                                    file_node, include_edge>;

} // namespace IncludeGuardian

#endif