#ifndef INCLUDE_GUARD_F13E361F_5253_4F12_A048_0155600793D3
#define INCLUDE_GUARD_F13E361F_5253_4F12_A048_0155600793D3

// We want to determine whether we have a component (header + source
// file) that is not included (or very rarely included) and list
// these as a potential to remove.
//
// For example, given the set of files below:
//
//   +-----------------------------------+
//   | foo.cpp     main.cpp      bar.cpp |
//   |      \              \    /        |
//   |       \              \  /         |
//   |      foo.hpp       bar.hpp        |
//   |         \            /  \         |
//   |          \          /    \        |
//   |           common.hpp    large.hpp |
//   |               |                   |
//   |               |                   |
//   |            zorb.hpp               |
//   +-----------------------------------+
//
// We should detect that `foo.cpp` and `foo.hpp` are a component
// and that nothing includes `foo.hpp`.  Here we should be wary of
// removing `main.cpp` because it (somewhat obviously) is not
// unused because it contains `int main()`.  Here we will only
// recommend a header+source pair to avoid this issue.

#include "graph.hpp"

#include <initializer_list>
#include <iosfwd>
#include <span>
#include <vector>

namespace IncludeGuardian {

struct component_and_cost {
  const file_node *source;
  cost saving;
};

bool operator==(const component_and_cost &lhs, const component_and_cost &rhs);
bool operator!=(const component_and_cost &lhs, const component_and_cost &rhs);
std::ostream &operator<<(std::ostream &out, const component_and_cost &v);

struct find_unused_components {
  /// Return the list of components in the specified `graph` with
  /// the specified `sources`, that are not included by more than
  /// `included_by_at_most` other files (not included the component's
  /// source file)
  static std::vector<component_and_cost>
  from_graph(const Graph &graph,
             std::span<const Graph::vertex_descriptor> sources,
             unsigned included_by_at_most = 0u);
  static std::vector<component_and_cost>
  from_graph(const Graph &graph,
             std::initializer_list<Graph::vertex_descriptor> sources,
             unsigned included_by_at_most = 0u);
};

} // namespace IncludeGuardian

#endif