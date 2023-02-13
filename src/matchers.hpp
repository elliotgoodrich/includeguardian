#ifndef INCLUDE_GUARD_57B0505D_DFAF_4ED2_9188_D72DCE9F288B
#define INCLUDE_GUARD_57B0505D_DFAF_4ED2_9188_D72DCE9F288B

#include "graph.hpp"

#include <gmock/gmock.h>

namespace IncludeGuardian {

inline bool vertices_equal(const file_node &lhs, const Graph &lgraph,
                           const file_node &rhs, const Graph &rgraph) {
  if (lhs.path != rhs.path || lhs.is_external != rhs.is_external ||
      lhs.underlying_cost != rhs.underlying_cost ||
      lhs.internal_incoming != rhs.internal_incoming ||
      lhs.external_incoming != rhs.external_incoming ||
      lhs.is_precompiled != rhs.is_precompiled) {
    return false;
  }

  if (lhs.component.has_value() != rhs.component.has_value()) {
    return false;
  }

  if (!lhs.component.has_value()) {
    return true;
  }

  if (lgraph[*lhs.component].path != rgraph[*rhs.component].path) {
    return false;
  }

  return true;
}

MATCHER_P2(VertexDescriptorIs, graph, matcher,
           "Whether a graph vertex descriptor matches something") {
  return Matches(matcher)(graph[arg]);
}

inline std::vector<include_edge> get_out_edges(Graph::vertex_descriptor v,
                                               const Graph &g) {
  std::vector<include_edge> out;
  const auto [edge_start, edge_end] = out_edges(v, g);
  std::transform(edge_start, edge_end, std::back_inserter(out),
                 [&g](Graph::edge_descriptor e) { return g[e]; });
  return out;
}

template <typename T> void dump(T &out, const std::vector<include_edge> &v) {
  if (v.empty()) {
    out << "[]";
    return;
  }
  out << '[';
  for (std::size_t i = 0; i < v.size() - 1; ++i) {
    out << v[i] << ", ";
  }
  out << v.back() << ']';
}

MATCHER_P(GraphsAreEquivalent, expected, "Whether two graphs compare equal") {
  using namespace testing;
  EXPECT_THAT(num_vertices(arg), Eq(num_vertices(expected)));

  // We need to use an `unordered_map` as we may build up our graph in the wrong
  // order that we encounter files during our C++ preprocessor step.
  std::unordered_map<std::string, Graph::vertex_descriptor> file_lookup(
      num_vertices(arg));

  for (const Graph::vertex_descriptor v :
       boost::make_iterator_range(vertices(arg))) {
    const bool is_new = file_lookup.emplace(arg[v].path.string(), v).second;
    if (!is_new) {
      *result_listener << "Duplicate path found " << arg[v].path;
      return false;
    }
  }

  for (const Graph::vertex_descriptor v :
       boost::make_iterator_range(vertices(expected))) {
    const auto it = file_lookup.find(expected[v].path.string());
    if (it == file_lookup.end()) {
      *result_listener << "Could not find " << expected[v].path;
      return false;
    }

    if (!vertices_equal(arg[it->second], arg, expected[v], expected)) {
      *result_listener << "file_nodes do not compare equal " << arg[it->second]
                       << " != " << expected[v];
      return false;
    }

    // TODO, we don't check the target of our edges to make sure
    // that they match
    const std::vector<include_edge> l_edges = get_out_edges(it->second, arg);
    const std::vector<include_edge> r_edges = get_out_edges(v, expected);
    if (l_edges != r_edges) {
      *result_listener << "out_edges are not the same ";
      dump(*result_listener, l_edges);
      *result_listener << " != ";
      dump(*result_listener, r_edges);
      return false;
    }
  }
  return true;
}

} // namespace IncludeGuardian

#endif
