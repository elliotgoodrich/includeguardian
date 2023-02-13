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
class DiamondGraph : public ::testing::Test {
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

private:
  const Graph::vertex_descriptor sources_arr[2];

protected:
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

//   a.c  main.c  b.c
//    |  /      \  |
//   a.h          b.h
class WInclude : public ::testing::Test {
protected:
  Graph graph;
  const cost A_H;
  const cost A_C;
  const cost B_H;
  const cost B_C;
  const cost MAIN_C;

  const Graph::vertex_descriptor a_h;
  const Graph::vertex_descriptor a_c;
  const Graph::vertex_descriptor b_h;
  const Graph::vertex_descriptor b_c;
  const Graph::vertex_descriptor main_c;

  const Graph::edge_descriptor a_link;
  const Graph::edge_descriptor b_link;
  const Graph::edge_descriptor main_to_a;
  const Graph::edge_descriptor main_to_b;

private:
  const Graph::vertex_descriptor sources_arr[3];

protected:
  WInclude();

  std::span<const Graph::vertex_descriptor> sources() const;
};

//   main.c  a.c
//       \  /
//       a.h  b.c
//         \  /
//         b.h  c.c
//           \  /
//           c.h  d.c
//             \  /
//             d.h
class CascadingInclude : public ::testing::Test {
protected:
  Graph graph;
  const cost A_H;
  const cost A_C;
  const cost B_H;
  const cost B_C;
  const cost C_H;
  const cost C_C;
  const cost D_H;
  const cost D_C;
  const cost MAIN_C;

  const Graph::vertex_descriptor a_h;
  const Graph::vertex_descriptor a_c;
  const Graph::vertex_descriptor b_h;
  const Graph::vertex_descriptor b_c;
  const Graph::vertex_descriptor c_h;
  const Graph::vertex_descriptor c_c;
  const Graph::vertex_descriptor d_h;
  const Graph::vertex_descriptor d_c;
  const Graph::vertex_descriptor main_c;

  const Graph::edge_descriptor a_link;
  const Graph::edge_descriptor b_link;
  const Graph::edge_descriptor c_link;
  const Graph::edge_descriptor d_link;
  const Graph::edge_descriptor a_to_b;
  const Graph::edge_descriptor b_to_c;
  const Graph::edge_descriptor c_to_d;
  const Graph::edge_descriptor main_to_a;

private:
  const Graph::vertex_descriptor sources_arr[5];

protected:
  CascadingInclude();

  std::span<const Graph::vertex_descriptor> sources() const;
};

//   main.c  a.c
//     | \   /
//     |  a.h   b.c ---.
//     |   \   /        \
//     |    b.h   c.c   s.h
//     |   / \   /
//     |  |   c.h   d.c
//     |   \   \   /   \
//     |    \   d.h     |
//     |     \          |
//     +------+------- e.h
//             \        |
//              '----- f.h
class ComplexCascadingInclude : public ::testing::Test {
protected:
  Graph graph;
  const cost A_H;
  const cost A_C;
  const cost B_H;
  const cost B_C;
  const cost C_H;
  const cost C_C;
  const cost D_H;
  const cost D_C;
  const cost E_H;
  const cost F_H;
  const cost S_H;
  const cost MAIN_C;

  const Graph::vertex_descriptor a_h;
  const Graph::vertex_descriptor a_c;
  const Graph::vertex_descriptor b_h;
  const Graph::vertex_descriptor b_c;
  const Graph::vertex_descriptor c_h;
  const Graph::vertex_descriptor c_c;
  const Graph::vertex_descriptor d_h;
  const Graph::vertex_descriptor d_c;
  const Graph::vertex_descriptor e_h;
  const Graph::vertex_descriptor f_h;
  const Graph::vertex_descriptor s_h;
  const Graph::vertex_descriptor main_c;

  const Graph::edge_descriptor a_link;
  const Graph::edge_descriptor b_link;
  const Graph::edge_descriptor c_link;
  const Graph::edge_descriptor d_link;
  const Graph::edge_descriptor a_to_b;
  const Graph::edge_descriptor b_to_c;
  const Graph::edge_descriptor b_to_f;
  const Graph::edge_descriptor b_to_s;
  const Graph::edge_descriptor c_to_d;
  const Graph::edge_descriptor d_to_e;
  const Graph::edge_descriptor e_to_f;
  const Graph::edge_descriptor main_to_a;
  const Graph::edge_descriptor main_to_e;

private:
  const Graph::vertex_descriptor sources_arr[5];

protected:
  ComplexCascadingInclude();

  std::span<const Graph::vertex_descriptor> sources() const;
};

//      a   (a is not a source)
//     / \
//    b   c
//     \ /
//      d
class NoSources : public DiamondGraph {
protected:
  using DiamondGraph::DiamondGraph;

  std::span<const Graph::vertex_descriptor> sources() const;
};



} // namespace IncludeGuardian

#endif
