#ifndef INCLUDE_GUARD_E31B79D8_2464_4823_BDE1_37F760251C13
#define INCLUDE_GUARD_E31B79D8_2464_4823_BDE1_37F760251C13

#include <boost/graph/adjacency_list.hpp>

#include <deque>
#include <execution>
#include <utility>
#include <vector>

namespace IncludeGuardian {

template <typename NODE, typename EDGE> class reachability_graph {
public:
  using handle =
      boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS, NODE,
                            EDGE>::vertex_descriptor;

private:
  std::size_t m_size;
  std::vector<char> m_paths;

  // For each unique path leading from `v` invoke `vis(vertex_descriptor)` for
  // each vertex in these paths.  Note that `vis` may visit the same vertext
  // multiple times if it is reachable from `v` through different paths.
  template <class IncidenceGraph, class Visitor>
  static void traverse_all_paths(
      const IncidenceGraph &g,
      const typename boost::graph_traits<IncidenceGraph>::vertex_descriptor v,
      Visitor vis) {
    std::deque<typename boost::graph_traits<IncidenceGraph>::vertex_descriptor>
        vs;
    vs.push_back(v);
    while (!vs.empty()) {
      const auto v = vs.front();
      vs.pop_front();
      if (vis(v)) {
		  const auto [begin, end] = adjacent_vertices(v, g);
		  vs.insert(vs.end(), begin, end);
	  }
    }
  }

public:
  // Create a `reachability_matrix`.
  explicit reachability_graph(
      const boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
                                  NODE, EDGE> &dag);

  reachability_graph(const reachability_graph &) = delete;

  // Return whether there is a path `from` to `to`.
  bool is_reachable(handle from, handle to) const;
};

template <typename NODE, typename EDGE>
reachability_graph<NODE, EDGE>::reachability_graph(
    const boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
                                NODE, EDGE> &dag)
    : m_size(num_vertices(dag))
    , m_paths(m_size * m_size) {
  const auto [begin, end] = vertices(dag);
  std::for_each(std::execution::par, begin, end, [&](const handle v) {
    // THINKING: It would be very good if we can reuse information
    // from previous vertex searches
    const std::size_t offset = v * m_size;
    traverse_all_paths(dag, v, [=](handle u) {
      const bool carry_on = !m_paths[offset + u];
      m_paths[offset + u] = true;
      return carry_on;
    });
  });
}

template <typename NODE, typename EDGE>
bool reachability_graph<NODE, EDGE>::is_reachable(handle from,
                                                            handle to) const {
  return m_paths[from * m_size + to];
}

} // namespace IncludeGuardian

#endif