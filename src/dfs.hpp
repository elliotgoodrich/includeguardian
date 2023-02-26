#ifndef INCLUDE_GUARD_583CFBCA_CC53_4D1D_8AAF_94091F3B5AE2
#define INCLUDE_GUARD_583CFBCA_CC53_4D1D_8AAF_94091F3B5AE2

// `dfs_helper` gives a range-like container over a DAG so that
// we can traverse nodes in a depth-first fashion

#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

namespace IncludeGuardian {

template <typename GRAPH> class dag_iterator {
public:
  typename GRAPH::vertex_descriptor *m_it;
  bool *m_seen;
  const GRAPH *m_graph;

  using iterator_category = std::input_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = typename GRAPH::vertex_descriptor;
  using pointer = const typename GRAPH::vertex_descriptor *;
  using reference = const typename GRAPH::vertex_descriptor &;

  dag_iterator(typename GRAPH::vertex_descriptor *stack)
      : m_it(stack), m_seen(nullptr), m_graph(nullptr) {}

  dag_iterator(const GRAPH &graph, bool *seen,
               typename GRAPH::vertex_descriptor *stack)
      : m_it(stack + 1), m_seen(seen + 1), m_graph(&graph) {
    assert(num_vertices(graph) > 0);
  }

  reference operator*() const { return *m_it; }
  pointer operator->() { return &*m_it; }

  dag_iterator &operator++() {
    // Pop this element and push all its children
    const typename GRAPH::vertex_descriptor v = *m_it;
    m_seen[v] = true;
    const auto [begin, end] = adjacent_vertices(v, *m_graph);
    m_it = std::prev(std::copy(begin, end, m_it));

    // Find the next unseen element in the stack
    while (m_seen[static_cast<std::int64_t>(*m_it)]) {
      --m_it;
    }
    return *this;
  }

  dag_iterator operator++(int) {
    dag_iterator tmp = *this;
    ++(*this);
    return tmp;
  }
};

template <typename GRAPH>
bool operator==(dag_iterator<GRAPH> lhs, dag_iterator<GRAPH> rhs) {
  return lhs.m_it == rhs.m_it;
}

template <typename GRAPH> struct temp_dag_range {
  const GRAPH *m_graph;
  typename GRAPH::vertex_descriptor m_start;
  bool *m_seen;
  typename GRAPH::vertex_descriptor *m_stack;

  temp_dag_range(const GRAPH &graph, typename GRAPH::vertex_descriptor start,
                 bool *seen, typename GRAPH::vertex_descriptor *stack)
      : m_graph(&graph), m_start(start), m_seen(seen), m_stack(stack) {}

  temp_dag_range& skipping(const typename GRAPH::vertex_descriptor v) {
      m_seen[v] = true;
      return *this;
  }

  dag_iterator<GRAPH> end() { return dag_iterator<GRAPH>(m_stack); }

  dag_iterator<GRAPH> begin() {
    const auto size = num_vertices(*m_graph) + 1;
    std::fill_n(m_seen, size, false);
    // We cast our `null_vertex` to a signed integer because we want to avoid
    // the cost of bounds checking in our `operator++`.  Technically
    // `null_vertex` isn't always -1 but it is in our case
    assert(static_cast<std::int64_t>(
               boost::graph_traits<GRAPH>::null_vertex()) == -1);
    std::fill_n(m_stack, size, -1);
    m_stack[1] = m_start;
    return dag_iterator<GRAPH>(*m_graph, m_seen, m_stack);
  }
};

template <typename GRAPH> class dfs_adaptor {
  const GRAPH &m_graph;
  std::unique_ptr<bool[]> m_seen;
  std::unique_ptr<typename GRAPH::vertex_descriptor[]> m_stack;

public:
  dfs_adaptor(const GRAPH &graph)
      : m_graph(graph), m_seen(new bool[num_vertices(graph) + 1]),
        m_stack(new
                typename GRAPH::vertex_descriptor[num_vertices(graph) + 1]) {}

  temp_dag_range<GRAPH> from(const typename GRAPH::vertex_descriptor source) {
    return {m_graph, source, m_seen.get(), m_stack.get()};
  }
};

} // namespace IncludeGuardian

#endif
