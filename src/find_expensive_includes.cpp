#include "find_expensive_includes.hpp"

#include <boost/graph/depth_first_search.hpp>
#include <boost/scope_exit.hpp>

#include <algorithm>
#include <cassert>
#include <iostream>

namespace IncludeGuardian {

namespace {

class DFSHelper {
  enum class search_state : std::uint8_t {
    not_seen,      // not found yet
    seen_initial,  // found in the first DFS
    seen_followup, // found in the second DFS
  };

  const Graph &m_graph;
  std::vector<search_state> m_state;
  std::vector<Graph::vertex_descriptor> m_stack;

public:
  explicit DFSHelper(const Graph &graph)
      : m_graph(graph), m_state(num_vertices(graph), search_state::not_seen),
        m_stack() {}

  // Return the total file size for all vertices that are unreachable from
  // `source` through `removed_edge` in the graph specified at constructon.
  std::size_t
  total_file_size_of_unreachable(Graph::vertex_descriptor from,
                                 Graph::edge_descriptor removed_edge) {
    const Graph::vertex_descriptor includee = target(removed_edge, m_graph);

    // Make sure that we reset our temporary variables
    BOOST_SCOPE_EXIT(&m_state, &m_stack) {
      std::fill(m_state.begin(), m_state.end(), search_state::not_seen);
      m_stack.clear();
    }
    BOOST_SCOPE_EXIT_END

    // Do a DFS from `source`, skipping the `removed_edge`, and add all vertices
    // found to `marked`.
    m_stack.push_back(from);
    while (!m_stack.empty()) {
      const Graph::vertex_descriptor v = m_stack.back();
      m_stack.pop_back();
      switch (m_state[v]) {
      case search_state::seen_followup:
        // We should always be correctly resetting `m_state` so shouldn't get
        // here
        [[unreachable]];
        assert(false);
      case search_state::seen_initial:
        // If we already saw this file we can skip it
        continue;
      case search_state::not_seen:
        // Do nothing
        break;
      }

      m_state[v] = search_state::seen_initial;
      for (const Graph::edge_descriptor &e :
           boost::make_iterator_range(out_edges(v, m_graph))) {

        // Don't traverse our `removed_edge`
        if (e == removed_edge) {
          continue;
        }

        // If we ever find `target(removed_edge)` then we have another path
        // to this file and we won't get anything by removing it
        const Graph::vertex_descriptor w = target(e, m_graph);
        if (w == includee) {
          return 0u;
        }

        m_stack.push_back(w);
      }
    }

    // If we never transitively included the file whose include directive
    // we're removing, then there's no gains to be had
    const Graph::vertex_descriptor includer = source(removed_edge, m_graph);
    if (m_state[includer] == search_state::not_seen) {
      return 0u;
    }

    std::size_t savingsInBytes = 0u;

    // Once all found vertices are marked, we DFS from `target(removed_edge)`
    // only looking at unmarked vertices and summing up their file sizes
    m_stack.push_back(includee);
    while (!m_stack.empty()) {
      const Graph::vertex_descriptor v = m_stack.back();
      m_stack.pop_back();
      switch (m_state[v]) {
      case search_state::seen_followup:
        // If we've already seen this, we can skip it
        continue;
      case search_state::not_seen:
        // If we didn't see this file when we skipped `removed_edge` then we
        // will get that saving
        savingsInBytes += m_graph[v].fileSizeInBytes;
        [[fallthrough]];
      case search_state::seen_initial:
        // If we already saw this file, we don't get a saving but need to
        // process its children.
        m_state[v] = search_state::seen_followup;
      }

      const auto [begin, end] = adjacent_vertices(v, m_graph);
      m_stack.insert(m_stack.end(), begin, end);
    }

    return savingsInBytes;
  }
};

} // namespace

bool operator==(const include_directive_and_cost &lhs,
                const include_directive_and_cost &rhs) {
  return lhs.file == rhs.file && lhs.include == rhs.include &&
         lhs.savingInBytes == rhs.savingInBytes;
}
bool operator!=(const include_directive_and_cost &lhs,
                const include_directive_and_cost &rhs) {
  return !(lhs == rhs);
}

std::ostream &operator<<(std::ostream &out,
                         const include_directive_and_cost &v) {
  return out << "[" << v.file << ", " << v.include << ", " << v.savingInBytes
             << ']';
}

std::vector<include_directive_and_cost> find_expensive_includes::from_graph(
    const Graph &graph, std::span<const Graph::vertex_descriptor> sources) {

  DFSHelper helper(graph);
  std::vector<include_directive_and_cost> results;
  for (const Graph::edge_descriptor &include :
       boost::make_iterator_range(edges(graph))) {
    std::size_t bytes_saved = 0u;
    for (const Graph::vertex_descriptor source : sources) {
      bytes_saved += helper.total_file_size_of_unreachable(source, include);
    }

    if (bytes_saved > 0u) {
      results.emplace_back(
          std::filesystem::path(graph[source(include, graph)].path),
          graph[include].code, bytes_saved);
    }
  }
  return results;
}

std::vector<include_directive_and_cost> find_expensive_includes::from_graph(
    const Graph &graph,
    std::initializer_list<Graph::vertex_descriptor> sources) {
  return from_graph(graph, std::span(sources.begin(), sources.end()));
}

} // namespace IncludeGuardian
