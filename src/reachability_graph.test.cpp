#include "reachability_graph.hpp"

#include "graph.hpp"

#include <gtest/gtest.h>

#include <filesystem>

using namespace IncludeGuardian;

namespace {

const auto B = boost::units::information::byte;
TEST(ReachabilityGraphTest, DiamondIncludes) {
  Graph g;
  const Graph::vertex_descriptor a = add_vertex({"a", 100 * B}, g);
  const Graph::vertex_descriptor b = add_vertex({"b", 1000 * B}, g);
  const Graph::vertex_descriptor c = add_vertex({"c", 2000 * B}, g);
  const Graph::vertex_descriptor d = add_vertex({"d", 30000 * B}, g);

  //      a
  //     / \
  //    b   c
  //     \ /
  //      d
  add_edge(a, b, {"a->b"}, g);
  add_edge(a, c, {"a->c"}, g);
  add_edge(b, d, {"b->d"}, g);
  add_edge(c, d, {"c->d"}, g);

  reachability_graph dag(g);
  EXPECT_EQ(dag.number_of_paths(a, a), 1);
  EXPECT_EQ(dag.number_of_paths(a, b), 1);
  EXPECT_EQ(dag.number_of_paths(a, c), 1);
  EXPECT_EQ(dag.number_of_paths(a, d), 2);

  EXPECT_EQ(dag.number_of_paths(b, a), 0);
  EXPECT_EQ(dag.number_of_paths(b, b), 1);
  EXPECT_EQ(dag.number_of_paths(b, c), 0);
  EXPECT_EQ(dag.number_of_paths(b, d), 1);

  EXPECT_EQ(dag.number_of_paths(c, a), 0);
  EXPECT_EQ(dag.number_of_paths(c, b), 0);
  EXPECT_EQ(dag.number_of_paths(c, c), 1);
  EXPECT_EQ(dag.number_of_paths(c, d), 1);

  EXPECT_EQ(dag.number_of_paths(d, a), 0);
  EXPECT_EQ(dag.number_of_paths(d, b), 0);
  EXPECT_EQ(dag.number_of_paths(d, c), 0);
  EXPECT_EQ(dag.number_of_paths(d, d), 1);
}

TEST(ReachabilityGraphTest, MultiLevel) {
  Graph graph;
  const Graph::vertex_descriptor a = add_vertex({"a", 0 * B}, graph);
  const Graph::vertex_descriptor b = add_vertex({"b", 0 * B}, graph);
  const Graph::vertex_descriptor c = add_vertex({"c", 0 * B}, graph);
  const Graph::vertex_descriptor d = add_vertex({"d", 0 * B}, graph);
  const Graph::vertex_descriptor e = add_vertex({"e", 0 * B}, graph);
  const Graph::vertex_descriptor f = add_vertex({"f", 0 * B}, graph);
  const Graph::vertex_descriptor g = add_vertex({"g", 0 * B}, graph);
  const Graph::vertex_descriptor h = add_vertex({"h", 0 * B}, graph);

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

  constexpr int SIZE = 8;
  ASSERT_EQ(num_vertices(graph), SIZE);

  const Graph::vertex_descriptor vs[SIZE] = {a, b, c, d, e, f, g, h};

  reachability_graph dag(graph);

  // clang-format off
  const int paths[SIZE][SIZE] = {
    //       a  b  c  d  e  f  g  h
    /* a */ {1, 0, 1, 1, 0, 2, 0, 2},
    /* b */ {0, 1, 0, 1, 1, 1, 1, 3},
    /* c */ {0, 0, 1, 0, 0, 1, 0, 1},
    /* d */ {0, 0, 0, 1, 0, 1, 0, 1},
    /* e */ {0, 0, 0, 0, 1, 0, 1, 2},
    /* f */ {0, 0, 0, 0, 0, 1, 0, 1},
    /* g */ {0, 0, 0, 0, 0, 0, 1, 1},
    /* h */ {0, 0, 0, 0, 0, 0, 0, 1},
  };
  // clang-format on

  const char alphabet[SIZE + 1] = "abcdefgh";
  for (int from = 0; from != SIZE; ++from) {
    for (int to = 0; to != SIZE; ++to) {
      EXPECT_EQ(dag.number_of_paths(vs[from], vs[to]), paths[from][to])
          << alphabet[from] << "->" << alphabet[to];
    }
  }
}

TEST(ReachabilityGraphTest, LongChain) {
  Graph graph;
  const Graph::vertex_descriptor a = add_vertex({"a", 0 * B}, graph);
  const Graph::vertex_descriptor b = add_vertex({"b", 0 * B}, graph);
  const Graph::vertex_descriptor c = add_vertex({"c", 0 * B}, graph);
  const Graph::vertex_descriptor d = add_vertex({"d", 0 * B}, graph);
  const Graph::vertex_descriptor e = add_vertex({"e", 0 * B}, graph);
  const Graph::vertex_descriptor f = add_vertex({"f", 0 * B}, graph);
  const Graph::vertex_descriptor g = add_vertex({"g", 0 * B}, graph);
  const Graph::vertex_descriptor h = add_vertex({"h", 0 * B}, graph);
  const Graph::vertex_descriptor i = add_vertex({"i", 0 * B}, graph);
  const Graph::vertex_descriptor j = add_vertex({"j", 0 * B}, graph);

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

  constexpr int SIZE = 10;
  ASSERT_EQ(num_vertices(graph), SIZE);

  const Graph::vertex_descriptor vs[SIZE] = {a, b, c, d, e, f, g, h, i, j};

  reachability_graph dag(graph);

  // clang-format off
  const int paths[SIZE][SIZE] = {
    //       a  b  c  d  e  f  g  h  i  j
    /* a */ {1, 1, 1, 2, 2, 2, 4, 4, 6, 10},
    /* b */ {0, 1, 0, 1, 1, 1, 2, 2, 3, 5},
    /* c */ {0, 0, 1, 1, 1, 1, 2, 2, 3, 5},
    /* d */ {0, 0, 0, 1, 1, 1, 2, 2, 3, 5},
    /* e */ {0, 0, 0, 0, 1, 0, 1, 1, 1, 2},
    /* f */ {0, 0, 0, 0, 0, 1, 1, 1, 2, 3},
    /* g */ {0, 0, 0, 0, 0, 0, 1, 1, 1, 2},
    /* h */ {0, 0, 0, 0, 0, 0, 0, 1, 0, 1},
    /* i */ {0, 0, 0, 0, 0, 0, 0, 0, 1, 1},
    /* j */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
  };
  // clang-format on

  const char alphabet[SIZE + 1] = "abcdefghij";
  for (int from = 0; from != SIZE; ++from) {
    for (int to = 0; to != SIZE; ++to) {
      EXPECT_EQ(dag.number_of_paths(vs[from], vs[to]), paths[from][to])
          << alphabet[from] << "->" << alphabet[to];
    }
  }
}

} // namespace
