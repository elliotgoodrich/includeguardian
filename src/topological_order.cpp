#include "topological_order.hpp"

#include <boost/graph/bellman_ford_shortest_paths.hpp>
#include <boost/graph/reverse_graph.hpp>
#include <boost/graph/strong_components.hpp>
#include <boost/property_map/function_property_map.hpp>

#include <unordered_map>
#include <utility>

namespace IncludeGuardian {

struct vertex {
  Graph::vertex_descriptor v;
};

std::vector<std::vector<std::vector<Graph::vertex_descriptor>>>
topological_order::from_graph(
    const Graph &original, std::span<const Graph::vertex_descriptor> sources) {
  if (sources.empty()) {
    return {};
  }

  // Create a new graph, with the edge direction reversed and with a
  // new "super root" vertex that has edges to all files that did
  // not include anything in the original graph
  using NewGraph =
      boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS>;
  NewGraph graph(num_vertices(original) + 1);
  const Graph::vertex_descriptor root = num_vertices(graph) - 1;

  for (const Graph::edge_descriptor &e :
       boost::make_iterator_range(edges(original))) {
    const Graph::vertex_descriptor t = target(e, original);
    if (!original[t].is_external) {
      add_edge(t, source(e, original), graph);
    }
  }
  for (const Graph::vertex_descriptor v :
       boost::make_iterator_range(vertices(original))) {
    if (out_degree(v, original) == 0 && !original[v].is_external) {
      add_edge(root, v, graph);
    }
  }

  // Connect up the components into a cycle so that they always exist
  // at the same level
  for (const Graph::vertex_descriptor source : sources) {
    const file_node &file = original[source];
    if (file.component.has_value()) {
      add_edge(source, *file.component, graph);
    }
  }

  // Create a map between each vertex and its component id
  const std::vector<int> component_map = [&] {
    std::vector<int> discover_time_tmp(num_vertices(graph));
    std::vector<boost::default_color_type> color_tmp(num_vertices(graph));
    std::vector<Graph::vertex_descriptor> tmp(num_vertices(graph));

    std::vector<int> component_map(num_vertices(graph));
    strong_components(
        graph,
        make_iterator_property_map(component_map.begin(),
                                   get(boost::vertex_index, graph)),
        root_map(make_iterator_property_map(tmp.begin(),
                                            get(boost::vertex_index, graph)))
            .color_map(make_iterator_property_map(
                color_tmp.begin(), get(boost::vertex_index, graph)))
            .discover_time_map(make_iterator_property_map(
                discover_time_tmp.begin(), get(boost::vertex_index, graph))));
    return component_map;
  }();

  // Use Dijkstra's Algorithm to find the shortest path to each
  // vertex, which will be the levelization number if the weight
  // of an edge is -1 if the vertices are in a different connected
  // component and 0 otherwise.  By using `-1` we will get the
  // longest path, which is the level.
  std::vector<int> levels(num_vertices(graph));
  const auto weight =
      boost::make_function_property_map<NewGraph::edge_descriptor>(
          [&](NewGraph::edge_descriptor e) {
            return -(component_map[source(e, graph)] !=
                     component_map[target(e, graph)]);
          });
  boost::bellman_ford_shortest_paths(
      graph, root, boost::distance_map(levels.data()).weight_map(weight));

  std::transform(levels.begin(), levels.end(), levels.begin(),
                 [](int w) { return -w; });

  const int num_levels = *std::max_element(levels.cbegin(), levels.cend());

  std::vector<std::vector<std::vector<Graph::vertex_descriptor>>> output(
      num_levels);
  const auto [begin, end] = vertices(original); // exclude the super root
  std::vector<Graph::vertex_descriptor> sorted_by_component(begin, end);
  std::stable_sort(
      sorted_by_component.begin(), sorted_by_component.end(),
      [&](const Graph::vertex_descriptor l, const Graph::vertex_descriptor r) {
        return component_map[l] < component_map[r];
      });

  auto it = sorted_by_component.cbegin();
  while (it != sorted_by_component.cend()) {
    const auto next =
        std::find_if(it + 1, sorted_by_component.cend(),
                     [component_id = component_map[*it],
                      &component_map](const Graph::vertex_descriptor v) {
                       return component_id != component_map[v];
                     });
    const int level = levels[*it];
    if (level != 0) {
      // We have a 0 level for the root or when there is an external
      // file with no includes since we do not connect it to the root
      output[levels[*it] - 1].emplace_back().assign(it, next);
    }
    it = next;
  }

  return output;
}

std::vector<std::vector<std::vector<Graph::vertex_descriptor>>>
topological_order::from_graph(
    const Graph &graph,
    std::initializer_list<Graph::vertex_descriptor> sources) {
  return from_graph(graph, std::span(sources.begin(), sources.end()));
}

} // namespace IncludeGuardian
