#ifndef INCLUDE_GUARD_9F9FADBD_FE08_4D31_A6FE_880C0A47216F
#define INCLUDE_GUARD_9F9FADBD_FE08_4D31_A6FE_880C0A47216F

#include "graph.hpp"

#include <gtest/gtest.h>

#include <span>

namespace IncludeGuardian {

//      a
//     / \
//    b   c
//     \ /
//      d
class DiamondGraph: public ::testing::Test {
protected:
  Graph graph;
  const cost A;
  const cost B;
  const cost C;
  const cost D;

  const Graph::vertex_descriptor a;
  const Graph::vertex_descriptor b;
  const Graph::vertex_descriptor c;
  const Graph::vertex_descriptor d;

  const Graph::edge_descriptor a_to_b;
  const Graph::edge_descriptor a_to_c;
  const Graph::edge_descriptor b_to_d;
  const Graph::edge_descriptor c_to_d;

  DiamondGraph();

  std::span<const Graph::vertex_descriptor> sources() const;
};

//      a   b
//     / \ / \
//    c   d  e
//     \ /  / \
//      f  g  /
//       \ | /
//         h
class MultiLevel : public ::testing::Test {
protected:
  Graph graph;
  const cost A;
  const cost B;
  const cost C;
  const cost D;
  const cost E;
  const cost F;
  const cost G;
  const cost H;

  const Graph::vertex_descriptor a;
  const Graph::vertex_descriptor b;
  const Graph::vertex_descriptor c;
  const Graph::vertex_descriptor d;
  const Graph::vertex_descriptor e;
  const Graph::vertex_descriptor f;
  const Graph::vertex_descriptor g;
  const Graph::vertex_descriptor h;

  const Graph::edge_descriptor a_to_c;
  const Graph::edge_descriptor a_to_d;
  const Graph::edge_descriptor b_to_d;
  const Graph::edge_descriptor b_to_e;
  const Graph::edge_descriptor c_to_f;
  const Graph::edge_descriptor d_to_f;
  const Graph::edge_descriptor e_to_g;
  const Graph::edge_descriptor e_to_h;
  const Graph::edge_descriptor f_to_h;
  const Graph::edge_descriptor g_to_h;

  MultiLevel();

  std::span<const Graph::vertex_descriptor> sources() const;
};

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
class LongChain : public ::testing::Test {
protected:
  Graph graph;
  const cost A;
  const cost B;
  const cost C;
  const cost D;
  const cost E;
  const cost F;
  const cost G;
  const cost H;
  const cost I;
  const cost J;

  const Graph::vertex_descriptor a;
  const Graph::vertex_descriptor b;
  const Graph::vertex_descriptor c;
  const Graph::vertex_descriptor d;
  const Graph::vertex_descriptor e;
  const Graph::vertex_descriptor f;
  const Graph::vertex_descriptor g;
  const Graph::vertex_descriptor h;
  const Graph::vertex_descriptor i;
  const Graph::vertex_descriptor j;

  const Graph::edge_descriptor a_to_b;
  const Graph::edge_descriptor a_to_c;
  const Graph::edge_descriptor b_to_d;
  const Graph::edge_descriptor c_to_d;
  const Graph::edge_descriptor d_to_e;
  const Graph::edge_descriptor d_to_f;
  const Graph::edge_descriptor e_to_g;
  const Graph::edge_descriptor f_to_g;
  const Graph::edge_descriptor f_to_i;
  const Graph::edge_descriptor g_to_h;
  const Graph::edge_descriptor g_to_i;
  const Graph::edge_descriptor h_to_j;
  const Graph::edge_descriptor i_to_j;

  LongChain();

  std::span<const Graph::vertex_descriptor> sources() const;
};

} // namespace IncludeGuardian

#endif
