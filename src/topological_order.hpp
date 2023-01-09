#ifndef INCLUDE_GUARD_99DA0450_09B8_456D_8377_B830EB496196
#define INCLUDE_GUARD_99DA0450_09B8_456D_8377_B830EB496196

#include "graph.hpp"

#include <initializer_list>
#include <span>
#include <vector>

namespace IncludeGuardian {

/// This component will list out all files in a topological order,
/// grouped by cycles and providing a "levelization" number for each
/// group, i.e. the maximum number of steps needed to reach elements
/// of this group from a group with no parents.
struct topological_order {
  static std::vector<std::vector<std::vector<Graph::vertex_descriptor>>>
  from_graph(const Graph &graph,
             std::span<const Graph::vertex_descriptor> sources);

  static std::vector<std::vector<std::vector<Graph::vertex_descriptor>>>
  from_graph(const Graph &graph,
             std::initializer_list<Graph::vertex_descriptor> sources);
};

} // namespace IncludeGuardian

#endif