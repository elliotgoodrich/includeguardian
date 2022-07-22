#include "list_included_files.hpp"

#include <algorithm>
#include <atomic>
#include <execution>
#include <ostream>

namespace IncludeGuardian {

std::vector<list_included_files::result> list_included_files::from_graph(
    const Graph &graph, std::span<const Graph::vertex_descriptor> sources) {

  std::vector<std::atomic<unsigned>> count(num_vertices(graph));
  std::for_each(std::execution::par, sources.begin(), sources.end(),
                [&](const Graph::vertex_descriptor source) {
                  std::vector<bool> seen(num_vertices(graph));
                  std::vector<Graph::vertex_descriptor> stack;
                  stack.push_back(source);

                  cost total;
                  while (!stack.empty()) {
                    const Graph::vertex_descriptor v = stack.back();
                    stack.pop_back();
                    if (seen[v]) {
                      continue;
                    }

                    seen[v] = true;
                    count[v].fetch_add(1);

                    const auto [begin, end] = adjacent_vertices(v, graph);
                    stack.insert(stack.end(), begin, end);
                  }
                  return total;
                });

  std::vector<result> r;
  r.reserve(num_vertices(graph));
  for (std::size_t i = 0; i < count.size(); ++i) {
    r.emplace_back(i, count[i].load());
  }
  return r;
}

std::vector<list_included_files::result> list_included_files::from_graph(
    const Graph &graph,
    std::initializer_list<Graph::vertex_descriptor> sources) {
  return list_included_files::from_graph(
      graph, std::span(sources.begin(), sources.end()));
}

bool operator==(const list_included_files::result &lhs,
                const list_included_files::result &rhs) {
  return lhs.v == rhs.v && lhs.source_that_can_reach_it_count ==
                               rhs.source_that_can_reach_it_count;
}

bool operator!=(const list_included_files::result &lhs,
                const list_included_files::result &rhs) {
  return !(lhs == rhs);
}

std::ostream &operator<<(std::ostream &out,
                         const list_included_files::result &v) {
  return out << '[' << v.v << " x" << v.source_that_can_reach_it_count << ']';
}

} // namespace IncludeGuardian
