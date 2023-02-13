#include "find_unused_components.hpp"

#include "get_total_cost.hpp"

#include <boost/units/io.hpp>

#include <execution>
#include <iomanip>
#include <numeric>
#include <ostream>

namespace IncludeGuardian {

bool operator==(const component_and_cost &lhs, const component_and_cost &rhs) {
  return lhs.source == rhs.source;
}

bool operator!=(const component_and_cost &lhs, const component_and_cost &rhs) {
  return !(lhs == rhs);
}

std::ostream &operator<<(std::ostream &out, const component_and_cost &v) {
  return out << *v.source;
}

std::vector<component_and_cost> find_unused_components::from_graph(
    const Graph &graph, std::span<const Graph::vertex_descriptor> sources,
    unsigned included_by_at_most, int minimum_token_count_cut_off) {
  std::mutex m;
  std::vector<component_and_cost> results;
  std::for_each(
      std::execution::par, sources.begin(), sources.end(),
      [&](const Graph::vertex_descriptor v) {
        const boost::optional<Graph::vertex_descriptor> &header =
            graph[v].component;
        if (!header) {
          return;
        }

        // Don't forget to add 1 to account for that component's
        // source include
        if (in_degree(*header, graph) <= included_by_at_most + 1 &&
            get_total_cost::from_graph(graph, {v}).true_cost.token_count >=
                minimum_token_count_cut_off) {
          std::lock_guard g(m);
          // There are ways to avoid this mutex, but if the
          // `minimum_size_cut_off` is large enough, it's relatively
          // rare to enter this if statement
          results.emplace_back(
              &graph[v], get_total_cost::from_graph(graph, {v}).true_cost);
        }
      });
  return results;
}

std::vector<component_and_cost> find_unused_components::from_graph(
    const Graph &graph, std::initializer_list<Graph::vertex_descriptor> sources,
    unsigned included_by_at_most, int minimum_token_count_cut_off) {
  return from_graph(graph, std::span(sources.begin(), sources.end()),
                    included_by_at_most, minimum_token_count_cut_off);
}

} // namespace IncludeGuardian
