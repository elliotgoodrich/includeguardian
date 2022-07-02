#include "get_total_cost.hpp"

#include "graph.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

const auto B = boost::units::information::byte;

const bool not_external = false;

TEST(GetTotalCostTest, DiamondIncludes) {
  Graph graph;
  const Graph::vertex_descriptor a =
      add_vertex({"a", not_external, 101u, 0b1000 * B}, graph);
  const Graph::vertex_descriptor b =
      add_vertex({"b", not_external, 102u, 0b0100 * B}, graph);
  const Graph::vertex_descriptor c =
      add_vertex({"c", not_external, 104u, 0b0010 * B}, graph);
  const Graph::vertex_descriptor d =
      add_vertex({"d", not_external, 108u, 0b0001 * B}, graph);

  //      a
  //     / \
  //    b   c
  //     \ /
  //      d
  add_edge(a, b, {"a->b"}, graph);
  add_edge(a, c, {"a->c"}, graph);
  add_edge(b, d, {"b->d"}, graph);
  add_edge(c, d, {"c->d"}, graph);

  EXPECT_EQ(get_total_cost::from_graph(graph, {a}),
            get_total_cost::result(0b1111 * B, 415u));
}

TEST(GetTotalCostTest, MultiLevel) {
  Graph graph;
  const Graph::vertex_descriptor a =
      add_vertex({"a", not_external, 101u, 0b1000'0000 * B}, graph);
  const Graph::vertex_descriptor b =
      add_vertex({"b", not_external, 102u, 0b0100'0000 * B}, graph);
  const Graph::vertex_descriptor c =
      add_vertex({"c", not_external, 104u, 0b0010'0000 * B}, graph);
  const Graph::vertex_descriptor d =
      add_vertex({"d", not_external, 108u, 0b0001'0000 * B}, graph);
  const Graph::vertex_descriptor e =
      add_vertex({"e", not_external, 116u, 0b0000'1000 * B}, graph);
  const Graph::vertex_descriptor f =
      add_vertex({"f", not_external, 132u, 0b0000'0100 * B}, graph);
  const Graph::vertex_descriptor g =
      add_vertex({"g", not_external, 164u, 0b0000'0010 * B}, graph);
  const Graph::vertex_descriptor h =
      add_vertex({"h", not_external, 228u, 0b0000'0001 * B}, graph);

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
            get_total_cost::result(0b1111'1111 * B + graph[d].file_size +
                                       graph[f].file_size + graph[h].file_size,
                                   1523u));
}

TEST(GetTotalCostTest, LongChain) {
  Graph graph;
  const Graph::vertex_descriptor a =
      add_vertex({"a", not_external, 1u, 0b10'0000'0000 * B}, graph);
  const Graph::vertex_descriptor b =
      add_vertex({"b", not_external, 1u, 0b01'0000'0000 * B}, graph);
  const Graph::vertex_descriptor c =
      add_vertex({"c", not_external, 1u, 0b00'1000'0000 * B}, graph);
  const Graph::vertex_descriptor d =
      add_vertex({"d", not_external, 1u, 0b00'0100'0000 * B}, graph);
  const Graph::vertex_descriptor e =
      add_vertex({"e", not_external, 1u, 0b00'0010'0000 * B}, graph);
  const Graph::vertex_descriptor f =
      add_vertex({"f", not_external, 1u, 0b00'0001'0000 * B}, graph);
  const Graph::vertex_descriptor g =
      add_vertex({"g", not_external, 1u, 0b00'0000'1000 * B}, graph);
  const Graph::vertex_descriptor h =
      add_vertex({"h", not_external, 1u, 0b00'0000'0100 * B}, graph);
  const Graph::vertex_descriptor i =
      add_vertex({"i", not_external, 1u, 0b00'0000'0010 * B}, graph);
  const Graph::vertex_descriptor j =
      add_vertex({"j", not_external, 1u, 0b00'0000'0001 * B}, graph);

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

  EXPECT_EQ(get_total_cost::from_graph(graph, {a}),
            get_total_cost::result(0b11'1111'1111 * B, 10u));
}

} // namespace
