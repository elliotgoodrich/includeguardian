#include "find_unnecessary_sources.hpp"

#include "reachability_graph.hpp"

#include <boost/units/io.hpp>

#include <execution>
#include <iomanip>
#include <numeric>
#include <ostream>

namespace IncludeGuardian {

namespace {

enum reachability {
  NONE = 0b00,
  HEADER = 0b01,
  SOURCE = 0b10,
};

} // namespace

std::vector<find_unnecessary_sources::result>
find_unnecessary_sources::from_graph(
    const Graph &graph, std::span<const Graph::vertex_descriptor> sources,
    const int minimum_token_count_cut_off) {
  reachability_graph reach(graph);
  std::mutex m;
  std::vector<result> results;

  const auto size = num_vertices(graph);
  std::for_each(
      std::execution::par, sources.begin(), sources.end(),
      [&](Graph::vertex_descriptor source) {
        // If we don't have an associate header then we probably can't do
        // anything.  TODO: report as an error.
        if (!graph[source].component.has_value()) {
          return;
        }

        // Skip external files as we don't have control over this and
        // most likely if the library has sources, it is already
        // compiled into a library and there is no additional cost when
        // we compile our code
        if (graph[source].is_external) {
          return;
        }

        std::vector<reachability> reachable(size, NONE);
        std::vector<Graph::vertex_descriptor> stack;

        // This will be the cost of all files that are reachable
        // from the source, but not from the header
        cost reachable_from_source_only;

        // Get total cost of `sources` and what it includes, also
        // mark all things reachable from `source`
        int num_reachable_from_source_only = 0;
        cost saving;
        stack.push_back(source);
        while (!stack.empty()) {
          const Graph::vertex_descriptor v = stack.back();
          stack.pop_back();
          if (reachable[v] & SOURCE) {
            continue;
          }

          reachable[v] = static_cast<reachability>(reachable[v] | SOURCE);
          saving += graph[v].cost;
          reachable_from_source_only += graph[v].cost;
          ++num_reachable_from_source_only;

          const auto [begin, end] = adjacent_vertices(v, graph);
          stack.insert(stack.end(), begin, end);
        }

        // If we won't save enough in the first place, exit early
        if (saving.token_count < minimum_token_count_cut_off) {
          return;
        }

        // Mark all files reachable from the header file
        stack.push_back(*graph[source].component);
        while (!stack.empty()) {
          const Graph::vertex_descriptor v = stack.back();
          stack.pop_back();
          if (reachable[v] & HEADER) {
            continue;
          }

          reachable[v] = static_cast<reachability>(reachable[v] | HEADER);
          --num_reachable_from_source_only;
          reachable_from_source_only -= graph[v].cost;

          const auto [begin, end] = adjacent_vertices(v, graph);
          stack.insert(stack.end(), begin, end);
        }

        // Go through all sources, for each source, do a DFS and find
        // all files that it reaches that are
        // sum up the size of all files in the set `S`.
        std::vector<cost> added_cost(size);
        const Graph::vertex_descriptor header = *graph[source].component;
        std::transform(
            std::execution::par, sources.begin(), sources.end(),
            added_cost.begin(), [&](Graph::vertex_descriptor start_source) {
              // We are removing `source` so don't add any analysis for it
              if (source == start_source) {
                return cost{};
              }

              // If we can't reach the header then there's no extra cost for
              // this source
              if (!reach.is_reachable(start_source, header)) {
                return cost{};
              }

              // Otherwise, assume we incur the full cost of the source file
              // and reduce `total` any time we encounter a file as part of
              // a DFS
              cost total = reachable_from_source_only;
              std::vector<bool> seen(size);
              int count = 0;
              std::vector<Graph::vertex_descriptor> stack;
              stack.push_back(start_source);
              while (!stack.empty() && count < num_reachable_from_source_only) {
                const Graph::vertex_descriptor v = stack.back();
                stack.pop_back();
                if (seen[v]) {
                  continue;
                }

                seen[v] = true;

                // We've assumed that each file reachable only from the source
                // will be an added cost, but if we can reach one of these
                // files some other way, then we need to subtract its cost.
                if (reachable[v] == SOURCE) {
                  --count;
                  total -= graph[v].cost;
                }

                const auto [begin, end] = adjacent_vertices(v, graph);
                stack.insert(stack.end(), begin, end);
              }

              return total;
            });

        const cost extra =
            std::accumulate(added_cost.cbegin(), added_cost.cend(), cost{});

        if (saving.token_count - extra.token_count >=
            minimum_token_count_cut_off) {
          // There are ways to avoid this mutex, but if the
          // `minimum_token_count_cut_off` is large enough, it's relatively rare
          // to enter this if statement
          std::lock_guard g(m);
          results.emplace_back(source, saving, extra);
        }
      });
  return results;
}

std::vector<find_unnecessary_sources::result>
find_unnecessary_sources::from_graph(
    const Graph &graph, std::initializer_list<Graph::vertex_descriptor> sources,
    const int minimum_token_count_cut_off) {
  return from_graph(graph, std::span(sources.begin(), sources.end()),
                    minimum_token_count_cut_off);
}

bool operator==(const find_unnecessary_sources::result &lhs,
                const find_unnecessary_sources::result &rhs) {
  return lhs.source == rhs.source && lhs.extra_cost == rhs.extra_cost &&
         lhs.saving == rhs.saving;
}

bool operator!=(const find_unnecessary_sources::result &lhs,
                const find_unnecessary_sources::result &rhs) {
  return !(lhs == rhs);
}

std::ostream &operator<<(std::ostream &out,
                         const find_unnecessary_sources::result &v) {
  return out << std::setprecision(2) << std::fixed << '[' << v.source
             << " saving=" << v.saving << " extra_cost=" << v.extra_cost << ']';
}

} // namespace IncludeGuardian
