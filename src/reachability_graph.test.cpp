#include "reachability_graph.hpp"

#include "analysis_test_fixtures.hpp"

#include <gtest/gtest.h>

using namespace IncludeGuardian;

namespace {

TEST_F(DiamondGraph, ReachabilityGraph) {
  reachability_graph dag(graph);
  EXPECT_EQ(dag.is_reachable(a, a), true);
  EXPECT_EQ(dag.is_reachable(a, b), true);
  EXPECT_EQ(dag.is_reachable(a, c), true);
  EXPECT_EQ(dag.is_reachable(a, d), true);

  EXPECT_EQ(dag.is_reachable(b, a), false);
  EXPECT_EQ(dag.is_reachable(b, b), true);
  EXPECT_EQ(dag.is_reachable(b, c), false);
  EXPECT_EQ(dag.is_reachable(b, d), true);

  EXPECT_EQ(dag.is_reachable(c, a), false);
  EXPECT_EQ(dag.is_reachable(c, b), false);
  EXPECT_EQ(dag.is_reachable(c, c), true);
  EXPECT_EQ(dag.is_reachable(c, d), true);

  EXPECT_EQ(dag.is_reachable(d, a), false);
  EXPECT_EQ(dag.is_reachable(d, b), false);
  EXPECT_EQ(dag.is_reachable(d, c), false);
  EXPECT_EQ(dag.is_reachable(d, d), true);
}

TEST_F(MultiLevel, ReachabilityGraph) {
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
      EXPECT_EQ(dag.is_reachable(vs[from], vs[to]), paths[from][to] > 0)
          << alphabet[from] << "->" << alphabet[to];
    }
  }
}

TEST_F(LongChain, ReachabilityGraph) {
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
      EXPECT_EQ(dag.is_reachable(vs[from], vs[to]), paths[from][to] > 0)
          << alphabet[from] << "->" << alphabet[to];
    }
  }
}

} // namespace
