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

class file_node;
class include_edge;

using Graph =
    boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS,
                          file_node, include_edge>;

class file_node {
public:
  std::filesystem::path path; //< Note that this will most likely be
                              //< a relative path (e.g. boost/foo.hpp) and
                              //< it will be unknown and generally unnecessary
                              //< as to what path it is relative to.
  bool is_external = false; //< Whether this file comes from an external library
  cost underlying_cost;
  std::optional<Graph::vertex_descriptor>
      component; //< If this is not null then this
                 //< either the corresponding source or header,
                 //< depending on whether this is the header or
                 //< source respectively.
  bool is_precompiled = false;
  unsigned internal_incoming =
      0; //< The number of times this file is included from non-external files
  unsigned external_incoming =
      0; //< The number of times this file is included from external files

  file_node();
  file_node(const std::filesystem::path &path);

  file_node &with_cost(
      long long int token_count,
      boost::units::quantity<boost::units::information::info> file_size) &;
  file_node &&with_cost(
      long long int token_count,
      boost::units::quantity<boost::units::information::info> file_size) &&;
  file_node &with_cost(cost c) &;
  file_node &&with_cost(cost c) &&;
  file_node &set_external(bool is_external) &;
  file_node &&set_external(bool is_external) &&;
  file_node &set_internal_parents(unsigned count) &;
  file_node &&set_internal_parents(unsigned count) &&;
  file_node &set_external_parents(unsigned count) &;
  file_node &&set_external_parents(unsigned count) &&;
  file_node &set_precompiled(bool is_precompiled) &;
  file_node &&set_precompiled(bool is_precompiled) &&;

  cost true_cost() const;
};

std::ostream &operator<<(std::ostream &stream, const file_node &value);

class include_edge {
public:
  std::string code;
  unsigned lineNumber;
  bool is_removable = true;
};

std::ostream &operator<<(std::ostream &stream, const include_edge &value);
bool operator==(const include_edge &lhs, const include_edge &rhs);
bool operator!=(const include_edge &lhs, const include_edge &rhs);

} // namespace IncludeGuardian

#endif