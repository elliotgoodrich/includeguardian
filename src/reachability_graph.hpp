#ifndef INCLUDE_GUARD_E31B79D8_2464_4823_BDE1_37F760251C13
#define INCLUDE_GUARD_E31B79D8_2464_4823_BDE1_37F760251C13

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/depth_first_search.hpp>
#include <boost/range/iterator_range.hpp>
#include <unordered_set>
#include <utility>
#include <vector>

namespace IncludeGuardian {

template <typename NODE, typename EDGE> class reachability_graph {
public:
  using handle =
      boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS, NODE,
                            EDGE>::vertex_descriptor;

private:
  std::vector<std::unordered_set<handle>> m_reachable;
  std::vector<std::uint32_t> m_paths;

  // For each unique path leading from `v` invoke `vis(vertex_descriptor)` for
  // each vertex in these paths.  Note that `vis` may visit the same vertext
  // multiple times if it is reachable from `v` through different paths.
  template <class IncidenceGraph, class Visitor>
  static void traverse_all_paths(
      const IncidenceGraph &g,
      typename boost::graph_traits<IncidenceGraph>::vertex_descriptor v,
      Visitor vis) {
    vis(v);
    for (auto &&edge : boost::make_iterator_range(boost::out_edges(v, g))) {
      traverse_all_paths(g, boost::target(edge, g), vis);
    }
  }

public:
  // Create a `reachability_matrix`.
  explicit reachability_graph(
      const boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
                                  NODE, EDGE> &dag);

  reachability_graph(const reachability_graph &) = delete;

  // Return a range of all handles that are reachable from `start`.  Note
  // that this will include `start` in the range.
  auto reachable_from(handle start) const;

  // Return the number of unique paths between the `from` handle to the `to`
  // handle.
  std::size_t number_of_paths(handle from, handle to) const;
};

template <typename NODE, typename EDGE>
reachability_graph<NODE, EDGE>::reachability_graph(
    const boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
                                NODE, EDGE> &dag)
    : m_reachable(boost::num_vertices(dag)),
      m_paths(boost::num_vertices(dag) * boost::num_vertices(dag)) {
  for (const handle v : boost::make_iterator_range(boost::vertices(dag))) {
    // THINKING: It would be very good if this could be parallelized and
    // also whether we can reuse information from previous vertex searches
    std::unordered_set<handle> &reachable = m_reachable[v];
    const std::size_t offset = v * m_reachable.size();
    traverse_all_paths(dag, v, [&](handle u) {
      reachable.emplace(u);
      ++m_paths[offset + u];
    });
  }
}

template <typename NODE, typename EDGE>
auto reachability_graph<NODE, EDGE>::reachable_from(handle start) const {
  return m_reachable[start];
}

template <typename NODE, typename EDGE>
std::size_t reachability_graph<NODE, EDGE>::number_of_paths(handle from,
                                                            handle to) const {
  return m_paths[from * m_reachable.size() + to];
}

} // namespace IncludeGuardian

#endif