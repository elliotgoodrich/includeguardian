#include "find_expensive_includes.hpp"

#include "reachability_graph.hpp"

namespace IncludeGuardian {

std::vector<include_directive_and_cost> find_expensive_includes::from_graph(
    const Graph &graph, std::span<const Graph::vertex_descriptor> sources) {
  reachability_graph dag(graph);
  std::vector<include_directive_and_cost> results;
  for (const Graph::vertex_descriptor v :
       boost::make_iterator_range(vertices(graph))) {
    for (const Graph::edge_descriptor directive :
         boost::make_iterator_range(out_edges(v, graph))) {
      const Graph::vertex_descriptor include = target(directive, graph);
      std::size_t bytes_saved = 0;

      for (const Graph::vertex_descriptor source : sources) {
        // Find the sum of the file sizes for `include` and all its
        // includes that are now not reachable if we removed it.
        for (const Graph::vertex_descriptor i : dag.reachable_from(v)) {
          // `i` is reachable from `source` only through `include` if the
          // number of paths between `source` and `i` is exactly the product
          // of the number of paths between `source` and `include`, and the
          // number of paths between `include` and `i`.
          if (dag.number_of_paths(source, include) *
                  dag.number_of_paths(include, i) ==
              dag.number_of_paths(source, i)) {
            bytes_saved += graph[include].fileSizeInBytes;
          }
        }
      }

      results.emplace_back(std::filesystem::path(graph[v].path),
                           graph[directive].code, bytes_saved);
    }
  }
  return results;
}

} // namespace IncludeGuardian
