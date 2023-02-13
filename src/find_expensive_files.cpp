#include "find_expensive_files.hpp"

#include "reachability_graph.hpp"

#include <boost/units/io.hpp>

#include <execution>
#include <iomanip>
#include <numeric>
#include <ostream>

namespace IncludeGuardian {

bool operator==(const file_and_cost &lhs, const file_and_cost &rhs) {
  return lhs.node == rhs.node && lhs.sources == rhs.sources;
}

bool operator!=(const file_and_cost &lhs, const file_and_cost &rhs) {
  return !(lhs == rhs);
}

std::ostream &operator<<(std::ostream &out, const file_and_cost &v) {
  return out << '[' << *v.node << " x" << std::setprecision(2) << std::fixed
             << v.sources << ']';
}

std::vector<file_and_cost> find_expensive_files::from_graph(
    const Graph &graph, std::span<const Graph::vertex_descriptor> sources,
    const int minimum_token_count_cut_off) {
  std::mutex m;
  std::vector<file_and_cost> results;
  if (sources.empty()) {
    return results;
  }

  reachability_graph reach(graph);

  const auto [begin, end] = vertices(graph);
  std::for_each(
      std::execution::par, begin, end,
      [&](const Graph::vertex_descriptor file) {
        // Ignore all files we have no control over
        if (graph[file].is_external) {
          return;
        }

        const double reachable_count = std::accumulate(
            sources.begin(), sources.end(), 0.0,
            [&](const double count, const Graph::vertex_descriptor source) {
              return count + reach.is_reachable(source, file);
            });

        if (reachable_count * graph[file].true_cost().token_count >=
            minimum_token_count_cut_off) {
          // There are ways to avoid this mutex, but if the
          // `minimum_size_cut_off` is large enough, it's relatively rare to
          // enter this if statement
          std::lock_guard g(m);
          results.emplace_back(&graph[file], reachable_count);
        }
      });
  return results;
}

std::vector<file_and_cost> find_expensive_files::from_graph(
    const Graph &graph, std::initializer_list<Graph::vertex_descriptor> sources,
    const int minimum_token_count_cut_off) {
  return from_graph(graph, std::span(sources.begin(), sources.end()),
                    minimum_token_count_cut_off);
}

} // namespace IncludeGuardian
