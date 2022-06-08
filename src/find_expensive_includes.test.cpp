#include "find_expensive_includes.hpp"

#include "graph.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

bool test_sort(const include_directive_and_cost &lhs,
               const include_directive_and_cost &rhs) {
  return std::tie(lhs.file, lhs.include->code, lhs.savingInBytes) <
         std::tie(rhs.file, rhs.include->code, rhs.savingInBytes);
}

TEST(FindExpensiveIncludesTest, DiamondIncludes) {
  Graph graph;
  const Graph::vertex_descriptor a = add_vertex({"a", 0b1000}, graph);
  const Graph::vertex_descriptor b = add_vertex({"b", 0b0100}, graph);
  const Graph::vertex_descriptor c = add_vertex({"c", 0b0010}, graph);
  const Graph::vertex_descriptor d = add_vertex({"d", 0b0001}, graph);

  //      a
  //     / \
  //    b   c
  //     \ /
  //      d
  const Graph::edge_descriptor a_to_b = add_edge(a, b, {"a->b"}, graph).first;
  const Graph::edge_descriptor a_to_c = add_edge(a, c, {"a->c"}, graph).first;
  const Graph::edge_descriptor b_to_d = add_edge(b, d, {"b->d"}, graph).first;
  const Graph::edge_descriptor c_to_d = add_edge(c, d, {"c->d"}, graph).first;

  std::vector<include_directive_and_cost> actual =
      find_expensive_includes::from_graph(graph, {a}, 1u);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<include_directive_and_cost> expected = {
      {"a", graph[b].fileSizeInBytes, &graph[a_to_b]},
      {"a", graph[c].fileSizeInBytes, &graph[a_to_c]},
  };
  EXPECT_EQ(actual, expected);
}

TEST(FindExpensiveIncludesTest, MultiLevel) {
  Graph graph;
  const Graph::vertex_descriptor a = add_vertex({"a", 0b1000'0000}, graph);
  const Graph::vertex_descriptor b = add_vertex({"b", 0b0100'0000}, graph);
  const Graph::vertex_descriptor c = add_vertex({"c", 0b0010'0000}, graph);
  const Graph::vertex_descriptor d = add_vertex({"d", 0b0001'0000}, graph);
  const Graph::vertex_descriptor e = add_vertex({"e", 0b0000'1000}, graph);
  const Graph::vertex_descriptor f = add_vertex({"f", 0b0000'0100}, graph);
  const Graph::vertex_descriptor g = add_vertex({"g", 0b0000'0010}, graph);
  const Graph::vertex_descriptor h = add_vertex({"h", 0b0000'0001}, graph);

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

  std::vector<include_directive_and_cost> actual =
      find_expensive_includes::from_graph(graph, {a, b}, 1u);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<include_directive_and_cost> expected = {
      {"a", graph[c].fileSizeInBytes, &graph[a_to_c]},
      {"a", graph[d].fileSizeInBytes, &graph[a_to_d]},
      {"b", graph[d].fileSizeInBytes + graph[f].fileSizeInBytes,
       &graph[b_to_d]},
      {"b", graph[e].fileSizeInBytes + graph[g].fileSizeInBytes,
       &graph[b_to_e]},
      {"d", graph[f].fileSizeInBytes, &graph[d_to_f]},
      {"e", graph[g].fileSizeInBytes, &graph[e_to_g]},
      {"f", graph[h].fileSizeInBytes, &graph[f_to_h]},
  };
  EXPECT_EQ(actual, expected);
}

TEST(FindExpensiveIncludesTest, LongChain) {
  Graph graph;
  const Graph::vertex_descriptor a = add_vertex({"a", 0b10'0000'0000}, graph);
  const Graph::vertex_descriptor b = add_vertex({"b", 0b01'0000'0000}, graph);
  const Graph::vertex_descriptor c = add_vertex({"c", 0b00'1000'0000}, graph);
  const Graph::vertex_descriptor d = add_vertex({"d", 0b00'0100'0000}, graph);
  const Graph::vertex_descriptor e = add_vertex({"e", 0b00'0010'0000}, graph);
  const Graph::vertex_descriptor f = add_vertex({"f", 0b00'0001'0000}, graph);
  const Graph::vertex_descriptor g = add_vertex({"g", 0b00'0000'1000}, graph);
  const Graph::vertex_descriptor h = add_vertex({"h", 0b00'0000'0100}, graph);
  const Graph::vertex_descriptor i = add_vertex({"i", 0b00'0000'0010}, graph);
  const Graph::vertex_descriptor j = add_vertex({"j", 0b00'0000'0001}, graph);

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

  std::vector<include_directive_and_cost> actual =
      find_expensive_includes::from_graph(graph, {a}, 1u);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<include_directive_and_cost> expected = {
      {"a", graph[b].fileSizeInBytes, &graph[a_to_b]},
      {"a", graph[c].fileSizeInBytes, &graph[a_to_c]},
      {"d", graph[e].fileSizeInBytes, &graph[d_to_e]},
      {"d", graph[f].fileSizeInBytes, &graph[d_to_f]},
      {"g", graph[h].fileSizeInBytes, &graph[g_to_h]},
  };
  EXPECT_EQ(actual, expected);
}

} // namespace
