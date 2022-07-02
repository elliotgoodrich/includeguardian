#ifndef INCLUDE_GUARD_0DC4C9E1_CE28_4D0C_9771_86480E7D991D
#define INCLUDE_GUARD_0DC4C9E1_CE28_4D0C_9771_86480E7D991D

#include "graph.hpp"

#include <filesystem>
#include <initializer_list>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace IncludeGuardian {

struct include_directive_and_cost {
  std::filesystem::path file;
  boost::units::quantity<boost::units::information::info> file_size;
  unsigned token_count;
  const include_edge *include;
};

bool operator==(const include_directive_and_cost &lhs,
                const include_directive_and_cost &rhs);
bool operator!=(const include_directive_and_cost &lhs,
                const include_directive_and_cost &rhs);
std::ostream &operator<<(std::ostream &out,
                         const include_directive_and_cost &v);

/// This component will output the include directives along with the total file
/// size that would be saved if it was deleted.
struct find_expensive_includes {
  static std::vector<include_directive_and_cost>
  from_graph(const Graph &graph,
             std::span<const Graph::vertex_descriptor> sources,
             unsigned minimum_token_count_cut_off = 0u);
  static std::vector<include_directive_and_cost>
  from_graph(const Graph &graph,
             std::initializer_list<Graph::vertex_descriptor> sources,
             unsigned minimum_token_count_cut_off = 0u);
};

} // namespace IncludeGuardian

#endif