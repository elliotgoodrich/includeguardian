#include "get_total_cost.hpp"

#include <boost/units/io.hpp>

#include <algorithm>
#include <execution>
#include <ostream>

namespace IncludeGuardian {

get_total_cost::result operator+(get_total_cost::result lhs,
                                 get_total_cost::result rhs) {
  return {lhs.true_cost + rhs.true_cost, lhs.precompiled + rhs.precompiled};
}

get_total_cost::result
get_total_cost::from_graph(const Graph &graph,
                           std::span<const Graph::vertex_descriptor> sources) {

  std::vector<result> source_cost(num_vertices(graph));
  std::transform(std::execution::par, sources.begin(), sources.end(),
                 source_cost.data(),
                 [&](const Graph::vertex_descriptor source) {
                   std::vector<bool> seen(num_vertices(graph));
                   std::vector<Graph::vertex_descriptor> stack;
                   stack.push_back(source);

                   result total;
                   while (!stack.empty()) {
                     const Graph::vertex_descriptor v = stack.back();
                     stack.pop_back();
                     if (seen[v]) {
                       continue;
                     }

                     seen[v] = true;
                     total.true_cost += graph[v].true_cost();
                     if (graph[v].is_precompiled) {
                       total.precompiled += graph[v].underlying_cost;
                     }

                     const auto [begin, end] = adjacent_vertices(v, graph);
                     stack.insert(stack.end(), begin, end);
                   }
                   return total;
                 });
  return std::reduce(source_cost.begin(), source_cost.end());
}

get_total_cost::result get_total_cost::from_graph(
    const Graph &graph,
    std::initializer_list<Graph::vertex_descriptor> sources) {
  return from_graph(graph, std::span(sources.begin(), sources.end()));
}

} // namespace IncludeGuardian
