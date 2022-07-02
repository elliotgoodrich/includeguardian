#include "get_total_cost.hpp"

#include <boost/scope_exit.hpp>
#include <boost/units/io.hpp>

#include <algorithm>
#include <ostream>
#include <cassert>

namespace IncludeGuardian {

namespace {

class DFSSizeHelper {
  enum class search_state : std::uint8_t {
    seen,
    not_seen,
  };

  const Graph &m_graph;
  std::vector<search_state> m_state;
  std::vector<Graph::vertex_descriptor> m_stack;

public:
  explicit DFSSizeHelper(const Graph &graph)
      : m_graph(graph), m_state(num_vertices(graph), search_state::not_seen),
        m_stack() {}

  // Return the total file size for all vertices that are reachable from
  // `source`.
  get_total_cost::result total_file_size(Graph::vertex_descriptor source) {

    get_total_cost::result total = {};
    // Make sure that we reset our temporary variables
    BOOST_SCOPE_EXIT(&m_state, &m_stack) {
      std::fill(m_state.begin(), m_state.end(), search_state::not_seen);
      m_stack.clear();
    }
    BOOST_SCOPE_EXIT_END

    // Do a DFS from `source`, skipping the `removed_edge`, and add all vertices
    // found to `marked`.
    m_stack.push_back(source);
    while (!m_stack.empty()) {
      const Graph::vertex_descriptor v = m_stack.back();
      m_stack.pop_back();
      if (m_state[v] == search_state::seen) {
        continue;
      }

      m_state[v] = search_state::seen;
      total.file_size += m_graph[v].file_size;
      total.token_count += m_graph[v].token_count;

      const auto [begin, end] = adjacent_vertices(v, m_graph);
      m_stack.insert(m_stack.end(), begin, end);
    }

    return total;
  }
};

} // namespace

get_total_cost::result
get_total_cost::from_graph(const Graph &graph,
                           std::span<const Graph::vertex_descriptor> sources) {
  DFSSizeHelper helper(graph);
  get_total_cost::result total = {};
  for (const Graph::vertex_descriptor source : sources) {
    const get_total_cost::result sub_total = helper.total_file_size(source);
    total.file_size += sub_total.file_size;
    total.token_count += sub_total.token_count;
  }
  return total;
}

get_total_cost::result get_total_cost::from_graph(
    const Graph &graph,
    std::initializer_list<Graph::vertex_descriptor> sources) {
  return from_graph(graph, std::span(sources.begin(), sources.end()));
}

bool operator==(const get_total_cost::result &lhs,
                const get_total_cost::result &rhs) {
    return lhs.file_size == rhs.file_size && lhs.token_count == rhs.token_count;
}
bool operator!=(const get_total_cost::result &lhs,
                const get_total_cost::result &rhs) {
    return !(lhs == rhs);
}
std::ostream &operator<<(std::ostream &out,
                         const get_total_cost::result &v) {
    return out << "file_size=" << v.file_size << " token_count=" << v.token_count;
}

} // namespace IncludeGuardian
