#include "get_total_cost.hpp"

#include "graph.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

TEST(GetTotalCostTest, DiamondIncludes) {
  return;
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
  add_edge(a, b, {"a->b"}, graph);
  add_edge(a, c, {"a->c"}, graph);
  add_edge(b, d, {"b->d"}, graph);
  add_edge(c, d, {"c->d"}, graph);

  EXPECT_EQ(get_total_cost::from_graph(graph, {a}), 0b1111);
}

TEST(GetTotalCostTest, MultiLevel) {
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
  add_edge(a, c, {"a->c"}, graph);
  add_edge(a, d, {"a->d"}, graph);
  add_edge(b, d, {"b->d"}, graph);
  add_edge(b, e, {"b->e"}, graph);
  add_edge(c, f, {"c->f"}, graph);
  add_edge(d, f, {"d->f"}, graph);
  add_edge(e, g, {"e->g"}, graph);
  add_edge(e, h, {"e->h"}, graph);
  add_edge(f, h, {"f->h"}, graph);
  add_edge(g, h, {"g->h"}, graph);

  EXPECT_EQ(get_total_cost::from_graph(graph, {a, b}),
            0b1111'1111 + graph[d].fileSizeInBytes + graph[f].fileSizeInBytes +
                graph[h].fileSizeInBytes);
}

TEST(GetTotalCostTest, LongChain) {
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
  add_edge(a, b, {"a->b"}, graph);
  add_edge(a, c, {"a->c"}, graph);
  add_edge(b, d, {"b->d"}, graph);
  add_edge(c, d, {"c->d"}, graph);
  add_edge(d, e, {"d->e"}, graph);
  add_edge(d, f, {"d->f"}, graph);
  add_edge(e, g, {"e->g"}, graph);
  add_edge(f, g, {"f->g"}, graph);
  add_edge(f, i, {"f->i"}, graph);
  add_edge(g, h, {"g->h"}, graph);
  add_edge(g, i, {"g->i"}, graph);
  add_edge(h, j, {"h->j"}, graph);
  add_edge(i, j, {"i->j"}, graph);

  EXPECT_EQ(get_total_cost::from_graph(graph, {a}), 0b11'1111'1111);
}

} // namespace
