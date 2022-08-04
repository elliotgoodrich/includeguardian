#include "find_expensive_headers.hpp"

#include "reachability_graph.hpp"

#ifndef NDEBUG
#include <boost/scope_exit.hpp>
#endif

#include <boost/units/io.hpp>

#include <execution>
#include <iomanip>
#include <numeric>
#include <ostream>

namespace IncludeGuardian {

namespace {

class DFSHelper {
  enum search_state : std::uint8_t {
    not_seen, // not found yet
    seen,     // found in the first DFS from `file`
    marked,
  };

  const Graph &m_graph;
  const reachability_graph<file_node, include_edge> &m_dag;
  std::vector<unsigned> m_state;
  std::vector<bool> m_reachable;
  std::vector<Graph::vertex_descriptor> m_stack;

public:
  DFSHelper(const Graph &graph,
            const reachability_graph<file_node, include_edge> &dag)
      : m_graph(graph), m_dag(dag), m_state(num_vertices(m_graph)),
        m_reachable(num_vertices(m_graph)), m_stack() {
    // Note that it's fine to leave `m_found` uninitialized since it's the first
    // thing we do in `total_file_size_of_unreachable`.
  }

  // Return the total file size for all vertices that are unreachable from
  // `source` if no files ever included `file` + an optional extra cost that
  // would occur if we needed to add a new source file.
  std::pair<cost, std::optional<cost>> total_file_size_of_unreachable(
      std::span<const Graph::vertex_descriptor> sources,
      Graph::vertex_descriptor file) {
    std::fill(m_state.begin(), m_state.end(), not_seen);
    m_reachable.resize(num_vertices(m_graph), false);

#ifndef NDEBUG
    // Make sure that we reset our temporary variable
    BOOST_SCOPE_EXIT(&m_stack) { assert(m_stack.empty()); }
    BOOST_SCOPE_EXIT_END
#endif
    std::vector<Graph::vertex_descriptor> descendents;

    cost total_size;

    // Do a DFS from `file` and add all vertices found to `marked`, summing
    // up the total cost of all files found
    m_stack.push_back(file);
    while (!m_stack.empty()) {
      const Graph::vertex_descriptor v = m_stack.back();
      m_stack.pop_back();
      if (m_state[v] == seen) {
        continue;
      }

      total_size += m_graph[v].true_cost();
      m_reachable[v] = true;
      m_state[v] = seen;
      descendents.emplace_back(v);
      const auto [begin, end] = adjacent_vertices(v, m_graph);
      m_stack.insert(m_stack.end(), begin, end);
    }

    cost savings;

    // Go through all sources that included `file` and find what dependencies
    // of `file` are reachable through other means.
    for (const Graph::vertex_descriptor source : sources) {
      // Do nothing if we're not reachable
      if (!m_dag.is_reachable(source, file)) {
        continue;
      }

      // If a source directly include `file`, then we gain nothing
      const auto [begin, end] = adjacent_vertices(source, m_graph);
      if (std::find(begin, end, file) != end) {
        continue;
      }

      // Reset the state before we can use it again
      std::fill(m_state.begin(), m_state.end(), not_seen);

      // Mark the file as already seen, so we have to DFS through other
      // paths
      m_state[file] = seen;

      // Assume we save the total cost of `file` and all its dependencies
      savings += total_size;

      // Once all found vertices are marked, we DFS from `file`
      // only looking at unmarked vertices and summing up their file sizes
      m_stack.push_back(source);
      while (!m_stack.empty()) {
        const Graph::vertex_descriptor v = m_stack.back();
        m_stack.pop_back();
        if (m_state[v] == seen) {
          continue;
        }

        if (m_reachable[v]) {
          // If we find something that's reachable not through `file`
          // then subtract it from our potential savings.
          savings -= m_graph[v].true_cost();
        }

        m_state[v] = seen;
        const auto [begin, end] = adjacent_vertices(v, m_graph);
        m_stack.insert(m_stack.end(), begin, end);
      }

      // Reset the state
      std::fill(m_state.begin(), m_state.end(), not_seen);
    }

    // If we don't have a source...
    if (!m_graph[file].component.has_value()) {
      // ...we would have to recommend adding a new source file that would
      // include `file`, costing `total_size`.
      return {savings, total_size};
    } else {
      // If we already have a source, there's no extra cost
      return {savings, std::nullopt};
    }
  }
};

} // namespace

std::vector<find_expensive_headers::result> find_expensive_headers::from_graph(
    const Graph &graph, std::span<const Graph::vertex_descriptor> sources,
    const int minimum_token_count_cut_off,
    const unsigned maximum_dependencies) {
  reachability_graph reach(graph);
  std::mutex m;
  std::vector<find_expensive_headers::result> results;
  const auto [begin, end] = vertices(graph);
  std::for_each(std::execution::par, begin, end,
                [&](const Graph::vertex_descriptor file) {
                  // If we do not include this file ourselves, there is no point
                  // performing the analysis
                  if (graph[file].internal_incoming == 0) {
                    return;
                  }

                  DFSHelper helper(graph, reach);
                  const auto [saving, extra] =
                      helper.total_file_size_of_unreachable(sources, file);

                  if (saving.token_count - extra.value_or(cost{}).token_count >=
                      minimum_token_count_cut_off) {
                    // There are ways to avoid this mutex, but if the
                    // `minimum_size_cut_off` is large enough, it's relatively
                    // rare to enter this if statement
                    std::lock_guard g(m);
                    results.emplace_back(file, saving, extra);
                  }
                });
  return results;
}

std::vector<find_expensive_headers::result> find_expensive_headers::from_graph(
    const Graph &graph, std::initializer_list<Graph::vertex_descriptor> sources,
    const int minimum_token_count_cut_off,
    const unsigned maximum_dependencies) {
  return from_graph(graph, std::span(sources.begin(), sources.end()),
                    minimum_token_count_cut_off, maximum_dependencies);
}

bool operator==(const find_expensive_headers::result &lhs,
                const find_expensive_headers::result &rhs) {
  return lhs.v == rhs.v && lhs.saving == rhs.saving &&
         lhs.new_source_cost == rhs.new_source_cost;
}

bool operator!=(const find_expensive_headers::result &lhs,
                const find_expensive_headers::result &rhs) {
  return !(lhs == rhs);
}

std::ostream &operator<<(std::ostream &out,
                         const find_expensive_headers::result &v) {
  out << '[' << v.v << " saving=" << v.saving;
  if (v.new_source_cost.has_value()) {
    out << "extra_cost=" << *v.new_source_cost;
  }
  return out << ']';
}

} // namespace IncludeGuardian
