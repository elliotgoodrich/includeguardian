#include "find_expensive_files.hpp"

#include "graph.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

const auto B = boost::units::information::byte;

bool test_sort(const file_and_cost &lhs,
               const file_and_cost &rhs) {
  return lhs.node->path < rhs.node->path;
}

TEST(FindExpensiveFilesTest, DiamondIncludes) {
  Graph graph;
  const Graph::vertex_descriptor a = add_vertex({"a", 0b1000 * B}, graph);
  const Graph::vertex_descriptor b = add_vertex({"b", 0b0100 * B}, graph);
  const Graph::vertex_descriptor c = add_vertex({"c", 0b0010 * B}, graph);
  const Graph::vertex_descriptor d = add_vertex({"d", 0b0001 * B}, graph);

  //      a
  //     / \
  //    b   c
  //     \ /
  //      d
  const Graph::edge_descriptor a_to_b = add_edge(a, b, {"a->b"}, graph).first;
  const Graph::edge_descriptor a_to_c = add_edge(a, c, {"a->c"}, graph).first;
  const Graph::edge_descriptor b_to_d = add_edge(b, d, {"b->d"}, graph).first;
  const Graph::edge_descriptor c_to_d = add_edge(c, d, {"c->d"}, graph).first;

  std::vector<file_and_cost> actual =
      find_expensive_files::from_graph(graph, {a}, 1 * B);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<file_and_cost> expected = {
      {&graph[a], 1},
      {&graph[b], 1},
      {&graph[c], 1},
      {&graph[d], 1},
  };
  EXPECT_EQ(actual, expected);
}

TEST(FindExpensiveFilesTest, MultiLevel) {
  Graph graph;
  const Graph::vertex_descriptor a = add_vertex({"a", 0b1000'0000 * B}, graph);
  const Graph::vertex_descriptor b = add_vertex({"b", 0b0100'0000 * B}, graph);
  const Graph::vertex_descriptor c = add_vertex({"c", 0b0010'0000 * B}, graph);
  const Graph::vertex_descriptor d = add_vertex({"d", 0b0001'0000 * B}, graph);
  const Graph::vertex_descriptor e = add_vertex({"e", 0b0000'1000 * B}, graph);
  const Graph::vertex_descriptor f = add_vertex({"f", 0b0000'0100 * B}, graph);
  const Graph::vertex_descriptor g = add_vertex({"g", 0b0000'0010 * B}, graph);
  const Graph::vertex_descriptor h = add_vertex({"h", 0b0000'0001 * B}, graph);

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

  std::vector<file_and_cost> actual =
      find_expensive_files::from_graph(graph, {a, b}, 1 * B);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<file_and_cost> expected = {
      {&graph[a], 1},
      {&graph[b], 1},
      {&graph[c], 1},
      {&graph[d], 2},
      {&graph[e], 1},
      {&graph[f], 2},
      {&graph[g], 1},
      {&graph[h], 2},
  };
  EXPECT_EQ(actual, expected);
}

TEST(FindExpensiveFilesTest, LongChain) {
  Graph graph;
  const Graph::vertex_descriptor a =
      add_vertex({"a", 0b10'0000'0000 * B}, graph);
  const Graph::vertex_descriptor b =
      add_vertex({"b", 0b01'0000'0000 * B}, graph);
  const Graph::vertex_descriptor c =
      add_vertex({"c", 0b00'1000'0000 * B}, graph);
  const Graph::vertex_descriptor d =
      add_vertex({"d", 0b00'0100'0000 * B}, graph);
  const Graph::vertex_descriptor e =
      add_vertex({"e", 0b00'0010'0000 * B}, graph);
  const Graph::vertex_descriptor f =
      add_vertex({"f", 0b00'0001'0000 * B}, graph);
  const Graph::vertex_descriptor g =
      add_vertex({"g", 0b00'0000'1000 * B}, graph);
  const Graph::vertex_descriptor h =
      add_vertex({"h", 0b00'0000'0100 * B}, graph);
  const Graph::vertex_descriptor i =
      add_vertex({"i", 0b00'0000'0010 * B}, graph);
  const Graph::vertex_descriptor j =
      add_vertex({"j", 0b00'0000'0001 * B}, graph);

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

  std::vector<file_and_cost> actual =
      find_expensive_files::from_graph(graph, {a}, 1 * B);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<file_and_cost> expected = {
      {&graph[a], 1},
      {&graph[b], 1},
      {&graph[c], 1},
      {&graph[d], 1},
      {&graph[e], 1},
      {&graph[f], 1},
      {&graph[g], 1},
      {&graph[h], 1},
      {&graph[i], 1},
      {&graph[j], 1},
  };
  EXPECT_EQ(actual, expected);
}

} // namespace