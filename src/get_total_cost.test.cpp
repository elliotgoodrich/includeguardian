#include "get_total_cost.hpp"

#include "graph.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

using namespace boost::units::information;

const bool not_external = false;

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

TEST(GetTotalCostTest, DiamondIncludes) {
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
  add_edge(a, b, {"a->b"}, graph);
  add_edge(a, c, {"a->c"}, graph);
  add_edge(b, d, {"b->d"}, graph);
  add_edge(c, d, {"c->d"}, graph);

  EXPECT_EQ(get_total_cost::from_graph(graph, {a}).true_cost, A + B + C + D);
}

TEST(GetTotalCostTest, MultiLevel) {
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

  EXPECT_EQ(get_total_cost::from_graph(graph, {a, b}).true_cost,
            (A + C + D + F + H) + (B + D + E + F + G + H));
}

TEST(GetTotalCostTest, LongChain) {
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

  EXPECT_EQ(get_total_cost::from_graph(graph, {a}).true_cost,
            A + B + C + D + E + F + G + H + I + J);
}

} // namespace
