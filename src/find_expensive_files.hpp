#ifndef INCLUDE_GUARD_AA4F6A18_E09D_419B_B133_5E8DDD0D995A
#define INCLUDE_GUARD_AA4F6A18_E09D_419B_B133_5E8DDD0D995A

#include "graph.hpp"

#include <initializer_list>
#include <iosfwd>
#include <span>
#include <vector>

namespace IncludeGuardian {

struct file_and_cost {
  const file_node *node;
  unsigned sources;
};

bool operator==(const file_and_cost &lhs, const file_and_cost &rhs);
bool operator!=(const file_and_cost &lhs, const file_and_cost &rhs);
std::ostream &operator<<(std::ostream &out, const file_and_cost &v);

/// This component will output the files along with the total file
/// size that would be saved if the size was reduced.
struct find_expensive_files {
  /// Return the list of files along with how many sources have a dependency
  /// on them, which would cause a total reduction in post-preprocessing
  /// file size of `minimum_size_cut_off` if that file's size was 0 bytes.
  static std::vector<file_and_cost>
  from_graph(const Graph &graph,
             std::span<const Graph::vertex_descriptor> sources,
             unsigned minimum_token_count_cut_off = 0u);
  static std::vector<file_and_cost>
  from_graph(const Graph &graph,
             std::initializer_list<Graph::vertex_descriptor> sources,
             unsigned minimum_token_count_cut_off = 0u);
};

} // namespace IncludeGuardian

#endif