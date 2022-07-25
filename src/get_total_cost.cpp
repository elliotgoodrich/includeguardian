#include "get_total_cost.hpp"

#include <boost/units/io.hpp>

#include <algorithm>
#include <execution>
#include <ostream>

namespace IncludeGuardian {

cost get_total_cost::from_graph(
    const Graph &graph, std::span<const Graph::vertex_descriptor> sources) {

  std::vector<cost> source_cost(num_vertices(graph));
  std::transform(std::execution::par, sources.begin(), sources.end(),
                 source_cost.data(),
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
                     total += graph[v].true_cost();

                     const auto [begin, end] = adjacent_vertices(v, graph);
                     stack.insert(stack.end(), begin, end);
                   }
                   return total;
                 });
  return std::reduce(source_cost.begin(), source_cost.end());
}

cost get_total_cost::from_graph(
    const Graph &graph,
    std::initializer_list<Graph::vertex_descriptor> sources) {
  return from_graph(graph, std::span(sources.begin(), sources.end()));
}

} // namespace IncludeGuardian
