#ifndef INCLUDE_GUARD_A229377A_F884_4D97_AD9A_0D6804EC7448
#define INCLUDE_GUARD_A229377A_F884_4D97_AD9A_0D6804EC7448

#include "graph.hpp"

#include <initializer_list>
#include <iosfwd>
#include <span>
#include <vector>

namespace IncludeGuardian {

struct find_expensive_headers {
    struct result {
        Graph::vertex_descriptor v;
        unsigned token_count;
  boost::units::quantity<boost::units::information::info> saved_file_size;
        unsigned sources_count;
    };

  /// Return the list of non-source files along with the total cost if
  /// no files ever included them.
  static std::vector<result>
  from_graph(const Graph &graph,
             std::span<const Graph::vertex_descriptor> sources,
             unsigned minimum_token_count_cut_off = 0u,
             unsigned maximum_dependencies = UINT_MAX);
  static std::vector<result>
  from_graph(const Graph &graph,
             std::initializer_list<Graph::vertex_descriptor> sources,
             unsigned minimum_token_count_cut_off = 0u,
             unsigned maximum_dependencies = UINT_MAX);
};

bool operator==(const find_expensive_headers::result &lhs, const find_expensive_headers::result &rhs);
bool operator!=(const find_expensive_headers::result &lhs, const find_expensive_headers::result &rhs);
std::ostream &operator<<(std::ostream &out, const find_expensive_headers::result &v);


} // namespace IncludeGuardian

#endif