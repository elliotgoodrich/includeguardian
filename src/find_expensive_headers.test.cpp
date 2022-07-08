#include "find_expensive_headers.hpp"

#include "graph.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

const auto B = boost::units::information::byte;

const bool not_external = false;

bool test_sort(const find_expensive_headers::result &lhs,
               const find_expensive_headers::result &rhs) {
  return std::tie(lhs.v, lhs.token_count) < std::tie(rhs.v, rhs.token_count);
}

TEST(FindExpensiveHeadersTest, DiamondIncludes) {
  Graph graph;
  const Graph::vertex_descriptor a =
      add_vertex({"a", not_external, 1u, 0b1000 * B}, graph);
  const Graph::vertex_descriptor b =
      add_vertex({"b", not_external, 2u, 0b0100 * B}, graph);
  const Graph::vertex_descriptor c =
      add_vertex({"c", not_external, 3u, 0b0010 * B}, graph);
  const Graph::vertex_descriptor d =
      add_vertex({"d", not_external, 4u, 0b0001 * B}, graph);

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
      find_expensive_headers::from_graph(graph, {a}, 1u);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<find_expensive_headers::result> expected = {
      {b, 2u, 0b0100 * B, 0u},
      {c, 3u, 0b0010 * B, 0u},
      {d, 4u, 0b0001 * B, 0u},
  };
  EXPECT_EQ(actual, expected);
}

TEST(FindExpensiveHeadersTest, MultiLevel) {
  Graph graph;
  const Graph::vertex_descriptor a =
      add_vertex({"a", not_external, 1u, 0b1000'0000 * B}, graph);
  const Graph::vertex_descriptor b =
      add_vertex({"b", not_external, 2u, 0b0100'0000 * B}, graph);
  const Graph::vertex_descriptor c =
      add_vertex({"c", not_external, 3u, 0b0010'0000 * B}, graph);
  const Graph::vertex_descriptor d =
      add_vertex({"d", not_external, 4u, 0b0001'0000 * B}, graph);
  const Graph::vertex_descriptor e =
      add_vertex({"e", not_external, 5u, 0b0000'1000 * B}, graph);
  const Graph::vertex_descriptor f =
      add_vertex({"f", not_external, 6u, 0b0000'0100 * B}, graph);
  const Graph::vertex_descriptor g =
      add_vertex({"g", not_external, 7u, 0b0000'0010 * B}, graph);
  const Graph::vertex_descriptor h =
      add_vertex({"h", not_external, 8u, 0b0000'0001 * B}, graph);

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
      find_expensive_headers::from_graph(graph, {a, b}, 1u);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<find_expensive_headers::result> expected = {
      {c, 3u, 0b0010'0000 * B, 0u},
      {d, 14u, 36.0 * B, 0u},
      {e, 12u, (0b0000'1000 + 0b0000'0010) * B, 0u},
      {f, 20u, 9.0 * B, 0u},
      {g, 7u, 0b0010 * B, 0u},
      {h, 16u, 2.0 * B, 0u},
  };
  EXPECT_EQ(actual, expected);
}

TEST(FindExpensiveHeadersTest, LongChain) {
  Graph graph;
  const Graph::vertex_descriptor a =
      add_vertex({"a", not_external, 1u, 0b10'0000'0000 * B}, graph);
  const Graph::vertex_descriptor b =
      add_vertex({"b", not_external, 2u, 0b01'0000'0000 * B}, graph);
  const Graph::vertex_descriptor c =
      add_vertex({"c", not_external, 4u, 0b00'1000'0000 * B}, graph);
  const Graph::vertex_descriptor d =
      add_vertex({"d", not_external, 8u, 0b00'0100'0000 * B}, graph);
  const Graph::vertex_descriptor e =
      add_vertex({"e", not_external, 16u, 0b00'0010'0000 * B}, graph);
  const Graph::vertex_descriptor f =
      add_vertex({"f", not_external, 32u, 0b00'0001'0000 * B}, graph);
  const Graph::vertex_descriptor g =
      add_vertex({"g", not_external, 64u, 0b00'0000'1000 * B}, graph);
  const Graph::vertex_descriptor h =
      add_vertex({"h", not_external, 128u, 0b00'0000'0100 * B}, graph);
  const Graph::vertex_descriptor i =
      add_vertex({"i", not_external, 256u, 0b00'0000'0010 * B}, graph);
  const Graph::vertex_descriptor j =
      add_vertex({"j", not_external, 512u, 0b00'0000'0001 * B}, graph);

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

  std::vector<find_expensive_headers::result> actual =
      find_expensive_headers::from_graph(graph, {a}, 1u);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<find_expensive_headers::result> expected = {
      {b, 2u, 0b01'0000'0000 * B, 0u}, {c, 4u, 0b1000'0000 * B, 0u},
      {d, 1016u, 127.0 * B, 0u},       {e, 16u, 32.0 * B, 0u},
      {f, 32u, 16.0 * B, 0u},          {g, 192u, 12.0 * B, 0u},
      {h, 128u, 4.0 * B, 0u},          {i, 256u, 2.0 * B, 0u},
      {j, 512u, 1.0 * B, 0u},
  };
  EXPECT_EQ(actual, expected);
}

} // namespace
