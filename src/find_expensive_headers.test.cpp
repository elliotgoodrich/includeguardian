#include "find_expensive_headers.hpp"

#include "graph.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

using namespace boost::units::information;

const bool not_external = false;

bool test_sort(const find_expensive_headers::result &lhs,
               const find_expensive_headers::result &rhs) {
  return lhs.v < rhs.v;
}

const cost A{1, 2000000000.0 * bytes};
const cost B{10, 200000000.0 * bytes};
const cost C{100, 20000000.0 * bytes};
const cost D{1000, 2000000.0 * bytes};
const cost E{10000, 200000.0 * bytes};
const cost F{100000, 20000.0 * bytes};
const cost G{1000000, 2000.0 * bytes};
const cost H{10000000, 200.0 * bytes};
const cost I{100000000, 20.0 * bytes};
const cost J{1000000000, 2.0 * bytes};

TEST(FindExpensiveHeadersTest, DiamondIncludes) {
  Graph graph;
  const Graph::vertex_descriptor a =
      add_vertex({"a", not_external, 0u, A}, graph);
  const Graph::vertex_descriptor b =
      add_vertex({"b", not_external, 1u, B}, graph);
  const Graph::vertex_descriptor c =
      add_vertex({"c", not_external, 1u, C}, graph);
  const Graph::vertex_descriptor d =
      add_vertex({"d", not_external, 2u, D}, graph);

  //      a
  //     / \
  //    b   c
  //     \ /
  //      d
  const Graph::edge_descriptor a_to_b = add_edge(a, b, {"a->b"}, graph).first;
  const Graph::edge_descriptor a_to_c = add_edge(a, c, {"a->c"}, graph).first;
  const Graph::edge_descriptor b_to_d = add_edge(b, d, {"b->d"}, graph).first;
  const Graph::edge_descriptor c_to_d = add_edge(c, d, {"c->d"}, graph).first;

  std::vector<find_expensive_headers::result> actual =
      find_expensive_headers::from_graph(graph, {a}, INT_MIN);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<find_expensive_headers::result> expected = {
      {b, cost{}, B + D},
      {c, cost{}, C + D},
      {d, D, D},
  };
  EXPECT_EQ(actual, expected);
}

TEST(FindExpensiveHeadersTest, MultiLevel) {
  Graph graph;
  const Graph::vertex_descriptor a =
      add_vertex({"a", not_external, 0u, A}, graph);
  const Graph::vertex_descriptor b =
      add_vertex({"b", not_external, 0u, B}, graph);
  const Graph::vertex_descriptor c =
      add_vertex({"c", not_external, 1u, C}, graph);
  const Graph::vertex_descriptor d =
      add_vertex({"d", not_external, 2u, D}, graph);
  const Graph::vertex_descriptor e =
      add_vertex({"e", not_external, 1u, E}, graph);
  const Graph::vertex_descriptor f =
      add_vertex({"f", not_external, 2u, F}, graph);
  const Graph::vertex_descriptor g =
      add_vertex({"g", not_external, 1u, G}, graph);
  const Graph::vertex_descriptor h =
      add_vertex({"h", not_external, 2u, H}, graph);

  //      a   b
  //     / \ / \
  //    c   d  e
  //     \ /  / \
  //      f  g  /
  //       \ | /
  //         h
  const Graph::edge_descriptor a_to_c = add_edge(a, c, {"a->c"}, graph).first;
  const Graph::edge_descriptor a_to_d = add_edge(a, d, {"a->d"}, graph).first;
  const Graph::edge_descriptor b_to_d = add_edge(b, d, {"b->d"}, graph).first;
  const Graph::edge_descriptor b_to_e = add_edge(b, e, {"b->e"}, graph).first;
  const Graph::edge_descriptor c_to_f = add_edge(c, f, {"c->f"}, graph).first;
  const Graph::edge_descriptor d_to_f = add_edge(d, f, {"d->f"}, graph).first;
  const Graph::edge_descriptor e_to_g = add_edge(e, g, {"e->g"}, graph).first;
  const Graph::edge_descriptor e_to_h = add_edge(e, h, {"e->h"}, graph).first;
  const Graph::edge_descriptor f_to_h = add_edge(f, h, {"f->h"}, graph).first;
  const Graph::edge_descriptor g_to_h = add_edge(g, h, {"g->h"}, graph).first;

  std::vector<find_expensive_headers::result> actual =
      find_expensive_headers::from_graph(graph, {a, b}, INT_MIN);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<find_expensive_headers::result> expected = {
      {c, cost{}, C + F + H}, {d, cost{}, D + F + H}, {e, cost{}, E + G + H},
      {f, F + F + H, F + H},  {g, G, G + H},          {h, H + H, H},
  };
  EXPECT_EQ(actual, expected);
}

TEST(FindExpensiveHeadersTest, LongChain) {
  Graph graph;
  const Graph::vertex_descriptor a =
      add_vertex({"a", not_external, 0u, A}, graph);
  const Graph::vertex_descriptor b =
      add_vertex({"b", not_external, 1u, B}, graph);
  const Graph::vertex_descriptor c =
      add_vertex({"c", not_external, 1u, C}, graph);
  const Graph::vertex_descriptor d =
      add_vertex({"d", not_external, 2u, D}, graph);
  const Graph::vertex_descriptor e =
      add_vertex({"e", not_external, 1u, E}, graph);
  const Graph::vertex_descriptor f =
      add_vertex({"f", not_external, 1u, F}, graph);
  const Graph::vertex_descriptor g =
      add_vertex({"g", not_external, 2u, G}, graph);
  const Graph::vertex_descriptor h =
      add_vertex({"h", not_external, 1u, H}, graph);
  const Graph::vertex_descriptor i =
      add_vertex({"i", not_external, 2u, I}, graph);
  const Graph::vertex_descriptor j =
      add_vertex({"j", not_external, 2u, J}, graph);

  //      a
  //     / \
  //    b   c
  //     \ /
  //      d
  //     / \
  //    e   f
  //     \ / \
  //      g   |
  //     / \ /
  //    h   i
  //     \ /
  //      j
  const Graph::edge_descriptor a_to_b = add_edge(a, b, {"a->b"}, graph).first;
  const Graph::edge_descriptor a_to_c = add_edge(a, c, {"a->c"}, graph).first;
  const Graph::edge_descriptor b_to_d = add_edge(b, d, {"b->d"}, graph).first;
  const Graph::edge_descriptor c_to_d = add_edge(c, d, {"c->d"}, graph).first;
  const Graph::edge_descriptor d_to_e = add_edge(d, e, {"d->e"}, graph).first;
  const Graph::edge_descriptor d_to_f = add_edge(d, f, {"d->f"}, graph).first;
  const Graph::edge_descriptor e_to_g = add_edge(e, g, {"e->g"}, graph).first;
  const Graph::edge_descriptor f_to_g = add_edge(f, g, {"f->g"}, graph).first;
  const Graph::edge_descriptor f_to_i = add_edge(f, i, {"f->i"}, graph).first;
  const Graph::edge_descriptor g_to_h = add_edge(g, h, {"g->h"}, graph).first;
  const Graph::edge_descriptor g_to_i = add_edge(g, i, {"g->i"}, graph).first;
  const Graph::edge_descriptor h_to_j = add_edge(h, j, {"h->j"}, graph).first;
  const Graph::edge_descriptor i_to_j = add_edge(i, j, {"i->j"}, graph).first;

  //      a
  //     / \
  //    b   c
  //     \ /
  //      d
  //     / \
  //    e   f
  //     \ / \
  //      g   |
  //     / \ /
  //    h   i
  //     \ /
  //      j
  std::vector<find_expensive_headers::result> actual =
      find_expensive_headers::from_graph(graph, {a}, INT_MIN);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<find_expensive_headers::result> expected = {
      {b, cost{}, B + D + E + F + G + H + I + J},
      {c, cost{}, C + D + E + F + G + H + I + J},
      {d, D + E + F + G + H + I + J, D + E + F + G + H + I + J},
      {e, E, E + G + H + I + J},
      {f, F, F + G + H + I + J},
      {g, G + H, G + H + I + J},
      {h, H, H + J},
      {i, I, I + J},
      {j, J, J},
  };
  EXPECT_EQ(actual, expected);
}

} // namespace
