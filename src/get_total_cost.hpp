#ifndef INCLUDE_GUARD_AF3B784D_80D5_41F4_9502_F3652DD261AE
#define INCLUDE_GUARD_AF3B784D_80D5_41F4_9502_F3652DD261AE

#include "graph.hpp"

#include <initializer_list>
#include <iosfwd>
#include <span>

namespace IncludeGuardian {

/// This component will output the total number of bytes and preprocessing tokens
/// if all the `source` were expanded after the preprocessing step.
struct get_total_cost {
  struct result {
    boost::units::quantity<boost::units::information::info> file_size;
    unsigned token_count;
  };

  static result from_graph(const Graph &graph,
                           std::span<const Graph::vertex_descriptor> sources);
  static result
  from_graph(const Graph &graph,
             std::initializer_list<Graph::vertex_descriptor> sources);
};

bool operator==(const get_total_cost::result &lhs,
                const get_total_cost::result &rhs);
bool operator!=(const get_total_cost::result &lhs,
                const get_total_cost::result &rhs);
std::ostream &operator<<(std::ostream &out,
                         const get_total_cost::result &v);

} // namespace IncludeGuardian

#endif