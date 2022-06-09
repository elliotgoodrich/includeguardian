#ifndef INCLUDE_GUARD_AF3B784D_80D5_41F4_9502_F3652DD261AE
#define INCLUDE_GUARD_AF3B784D_80D5_41F4_9502_F3652DD261AE

#include "graph.hpp"

#include <initializer_list>
#include <span>

namespace IncludeGuardian {

/// This component will output the total number of bytes if all the `source`
/// were expanded after the preprocessing step.
struct get_total_cost {
  static boost::units::quantity<boost::units::information::info>
  from_graph(const Graph &graph,
             std::span<const Graph::vertex_descriptor> sources);
  static boost::units::quantity<boost::units::information::info>
  from_graph(const Graph &graph,
             std::initializer_list<Graph::vertex_descriptor> sources);
};

} // namespace IncludeGuardian

#endif