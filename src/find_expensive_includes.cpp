#include "find_expensive_includes.hpp"

#ifdef NDEBUG
#include <boost/scope_exit.hpp>
#endif

#include <algorithm>
#include <cassert>
#include <execution>
#include <iostream>
#include <mutex>

namespace IncludeGuardian {

namespace {

class DFSHelper {
  enum class search_state : std::uint8_t {
    not_seen,      // not found yet
    seen_initial,  // found in the first DFS
    seen_followup, // found in the second DFS
  };

  const Graph &m_graph;
  std::unique_ptr<search_state[]> m_state;
  std::vector<Graph::vertex_descriptor> m_stack;

public:
  explicit DFSHelper(const Graph &graph)
      : m_graph(graph), m_state(std::make_unique_for_overwrite<search_state[]>(num_vertices(m_graph))),
        m_stack() {
    // Note that it's fine to leave `m_state` uninitialized since it's the first
    // thing we do in `total_file_size_of_unreachable`.
  }

  // Return the total file size for all vertices that are unreachable from
  // `source` through `removed_edge` in the graph specified at constructon.
  std::size_t
  total_file_size_of_unreachable(Graph::vertex_descriptor from,
                                 Graph::edge_descriptor removed_edge) {
    std::fill(m_state.get(), m_state.get() + num_vertices(m_graph),
              search_state::not_seen);

    const Graph::vertex_descriptor includee = target(removed_edge, m_graph);

#ifdef NDEBUG
    // Make sure that we reset our temporary variable
    BOOST_SCOPE_EXIT(&m_stack) { assert(m_stack.empty()); }
    BOOST_SCOPE_EXIT_END
#endif

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
          m_stack.clear();
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
    const Graph &graph, std::span<const Graph::vertex_descriptor> sources,
    const std::size_t minimum_size_cut_off) {

  std::mutex m;
  std::vector<include_directive_and_cost> results;
  const auto [begin, end] = edges(graph);
  std::for_each(
      std::execution::par, begin, end,
      [&](const Graph::edge_descriptor &include) {
        DFSHelper helper(graph);
        const std::size_t bytes_saved = std::accumulate(
            sources.begin(), sources.end(), static_cast<std::size_t>(0),
            [&](std::size_t acc, Graph::vertex_descriptor source) {
              return acc +
                     helper.total_file_size_of_unreachable(source, include);
            });

        if (bytes_saved >= minimum_size_cut_off) {
          // There are ways to avoid this mutex, but if the
          // `minimum_size_cut_off` is large enough, it's relatively rare to
          // enter this if statement
          std::lock_guard g(m);
          results.emplace_back(
              std::filesystem::path(graph[source(include, graph)].path),
              graph[include].code, bytes_saved);
        }
      });
  return results;
}

std::vector<include_directive_and_cost> find_expensive_includes::from_graph(
    const Graph &graph, std::initializer_list<Graph::vertex_descriptor> sources,
    const std::size_t minimum_size_cut_off) {
  return from_graph(graph, std::span(sources.begin(), sources.end()),
                    minimum_size_cut_off);
}

} // namespace IncludeGuardian
