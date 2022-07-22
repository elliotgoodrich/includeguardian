#ifndef INCLUDE_GUARD_E685EBC9_8546_4A67_8A47_188BC65EB5E6
#define INCLUDE_GUARD_E685EBC9_8546_4A67_8A47_188BC65EB5E6

#include "graph.hpp"

#include <initializer_list>
#include <span>

namespace IncludeGuardian {

/// This component will list out all individual files along side the
/// number of source files in which they are directly or indirectly
/// included.
struct list_included_files {
  struct result {
    Graph::vertex_descriptor v;
    unsigned source_that_can_reach_it_count;
  };

  static std::vector<result>
  from_graph(const Graph &graph,
             std::span<const Graph::vertex_descriptor> sources);
  static std::vector<result>
  from_graph(const Graph &graph,
             std::initializer_list<Graph::vertex_descriptor> sources);
};

bool operator==(const list_included_files::result &lhs,
                const list_included_files::result &rhs);
bool operator!=(const list_included_files::result &lhs,
                const list_included_files::result &rhs);
std::ostream &operator<<(std::ostream &out,
                         const list_included_files::result &v);

} // namespace IncludeGuardian

#endif