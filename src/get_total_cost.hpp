#ifndef INCLUDE_GUARD_AF3B784D_80D5_41F4_9502_F3652DD261AE
#define INCLUDE_GUARD_AF3B784D_80D5_41F4_9502_F3652DD261AE

#include "graph.hpp"

#include <initializer_list>
#include <span>

namespace IncludeGuardian {

/// This component will output the total number of bytes and preprocessing
/// tokens if all the `source` were expanded after the preprocessing step.
struct get_total_cost {
  struct result {
    cost true_cost;   //< The cost (excluding precompiled) of the graph
    cost precompiled; //< The cost of the precompiled header
    cost total() const {
        return true_cost + precompiled;
    }
  };
  static result from_graph(const Graph &graph,
                           std::span<const Graph::vertex_descriptor> sources);
  static result
  from_graph(const Graph &graph,
             std::initializer_list<Graph::vertex_descriptor> sources);
};

get_total_cost::result operator+(get_total_cost::result lhs,
                                 get_total_cost::result rhs);

} // namespace IncludeGuardian

#endif