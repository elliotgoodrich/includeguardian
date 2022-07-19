#include "find_unnecessary_sources.hpp"

#include "graph.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

using namespace boost::units::information;

const bool not_external = false;

bool test_sort(const find_unnecessary_sources::result &lhs,
               const find_unnecessary_sources::result &rhs) {
  return lhs.source < rhs.source;
}

const cost A_H{1, 20000000000.0 * bytes};
const cost A_C{10, 2000000000.0 * bytes};
const cost B_H{100, 200000000.0 * bytes};
const cost B_C{1000, 20000000.0 * bytes};
const cost C_H{10000, 2000000.0 * bytes};
const cost C_C{100000, 200000.0 * bytes};
const cost D_H{1000000, 20000.0 * bytes};
const cost D_C{10000000, 2000.0 * bytes};
const cost E_H{100000000, 200.0 * bytes};
const cost F_H{1000000000, 20.0 * bytes};
const cost S_H{99, 2.0 * bytes};
const cost MAIN{12345, 98765.0 * bytes};

TEST(FindUnnecessarySourcesTest, WInclude) {
  Graph graph;
  const Graph::vertex_descriptor a_h =
      add_vertex({"a.h", not_external, 2u, A_H}, graph);
  const Graph::vertex_descriptor a_c =
      add_vertex({"a.c", not_external, 0u, A_C}, graph);
  const Graph::vertex_descriptor b_h =
      add_vertex({"b.h", not_external, 2u, B_H}, graph);
  const Graph::vertex_descriptor b_c =
      add_vertex({"b.c", not_external, 0u, B_C}, graph);
  const Graph::vertex_descriptor main =
      add_vertex({"main.c", not_external, 0u, MAIN}, graph);

  //   a.c  main.c  b.c
  //    |  /      \  |
  //   a.h          b.h

  const Graph::edge_descriptor a_link =
      add_edge(a_c, a_h, {"a->a"}, graph).first;
  const Graph::edge_descriptor b_link =
      add_edge(b_c, b_h, {"b->b"}, graph).first;
  const Graph::edge_descriptor main_to_a =
      add_edge(main, a_h, {"main->a"}, graph).first;
  const Graph::edge_descriptor main_to_b =
      add_edge(main, b_h, {"main->b"}, graph).first;

  graph[a_h].component = a_c;
  graph[a_c].component = a_h;
  graph[b_h].component = b_c;
  graph[b_c].component = b_h;

  std::vector<find_unnecessary_sources::result> actual =
      find_unnecessary_sources::from_graph(graph, {main, a_c, b_c}, INT_MIN);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<find_unnecessary_sources::result> expected = {
      {a_c, A_C + A_H, A_C},
      {b_c, B_C + B_H, B_C},
  };
  EXPECT_EQ(actual, expected);
}

TEST(FindUnnecessarySourcesTest, CascadingInclude) {
  Graph graph;
  const Graph::vertex_descriptor a_h =
      add_vertex({"a.h", not_external, 2u, A_H}, graph);
  const Graph::vertex_descriptor a_c =
      add_vertex({"a.c", not_external, 0u, A_C}, graph);
  const Graph::vertex_descriptor b_h =
      add_vertex({"b.h", not_external, 3u, B_H}, graph);
  const Graph::vertex_descriptor b_c =
      add_vertex({"b.c", not_external, 0u, B_C}, graph);
  const Graph::vertex_descriptor c_h =
      add_vertex({"c.h", not_external, 3u, C_H}, graph);
  const Graph::vertex_descriptor c_c =
      add_vertex({"c.c", not_external, 0u, C_C}, graph);
  const Graph::vertex_descriptor d_h =
      add_vertex({"d.h", not_external, 3u, D_H}, graph);
  const Graph::vertex_descriptor d_c =
      add_vertex({"d.c", not_external, 0u, D_C}, graph);
  const Graph::vertex_descriptor main =
      add_vertex({"main.c", not_external, 0u, MAIN}, graph);

  //   main.c  a.c
  //       \  /
  //       a.h  b.c
  //         \  /
  //         b.h  c.c
  //           \  /
  //           c.h  d.c
  //             \  /
  //             d.h

  const Graph::edge_descriptor a_link =
      add_edge(a_c, a_h, {"a->a"}, graph).first;
  const Graph::edge_descriptor b_link =
      add_edge(b_c, b_h, {"b->b"}, graph).first;
  const Graph::edge_descriptor c_link =
      add_edge(c_c, c_h, {"c->c"}, graph).first;
  const Graph::edge_descriptor d_link =
      add_edge(d_c, d_h, {"d->d"}, graph).first;
  const Graph::edge_descriptor a_to_b =
      add_edge(a_h, b_h, {"a->b"}, graph).first;
  const Graph::edge_descriptor b_to_c =
      add_edge(b_h, c_h, {"b->c"}, graph).first;
  const Graph::edge_descriptor c_to_d =
      add_edge(c_h, d_h, {"c->d"}, graph).first;
  const Graph::edge_descriptor main_to_a =
      add_edge(main, a_h, {"main->a"}, graph).first;

  graph[a_h].component = a_c;
  graph[a_c].component = a_h;
  graph[b_h].component = b_c;
  graph[b_c].component = b_h;
  graph[c_h].component = c_c;
  graph[c_c].component = c_h;
  graph[d_h].component = d_c;
  graph[d_c].component = d_h;

  std::vector<find_unnecessary_sources::result> actual =
      find_unnecessary_sources::from_graph(graph, {main, a_c, b_c, c_c, d_c},
                                           INT_MIN);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<find_unnecessary_sources::result> expected = {
      {a_c, A_C + A_H + B_H + C_H + D_H, A_C},
      {b_c, B_C + B_H + C_H + D_H, 2 * B_C},
      {c_c, C_C + C_H + D_H, 3 * C_C},
      {d_c, D_C + D_H, 4 * D_C},
  };
  EXPECT_EQ(actual, expected);
}

TEST(FindUnnecessarySourcesTest, ComplexCascadingInclude) {
  Graph graph;
  const auto a_h = add_vertex({"a.h", not_external, 2u, A_H}, graph);
  const auto a_c = add_vertex({"a.c", not_external, 0u, A_C}, graph);
  const auto b_h = add_vertex({"b.h", not_external, 3u, B_H}, graph);
  const auto b_c = add_vertex({"b.c", not_external, 0u, B_C}, graph);
  const auto c_h = add_vertex({"c.h", not_external, 3u, C_H}, graph);
  const auto c_c = add_vertex({"c.c", not_external, 0u, C_C}, graph);
  const auto d_h = add_vertex({"d.h", not_external, 3u, D_H}, graph);
  const auto d_c = add_vertex({"d.c", not_external, 0u, D_C}, graph);
  const auto e_h = add_vertex({"e.h", not_external, 1u, E_H}, graph);
  const auto f_h = add_vertex({"f.h", not_external, 1u, F_H}, graph);
  const auto s_h = add_vertex({"s.h", not_external, 1u, S_H}, graph);
  const auto main = add_vertex({"main.c", not_external, 0u, MAIN}, graph);

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

  const Graph::edge_descriptor a_link =
      add_edge(a_c, a_h, {"a->a"}, graph).first;
  const Graph::edge_descriptor b_link =
      add_edge(b_c, b_h, {"b->b"}, graph).first;
  const Graph::edge_descriptor c_link =
      add_edge(c_c, c_h, {"c->c"}, graph).first;
  const Graph::edge_descriptor d_link =
      add_edge(d_c, d_h, {"d->d"}, graph).first;
  const Graph::edge_descriptor a_to_b =
      add_edge(a_h, b_h, {"a->b"}, graph).first;
  const Graph::edge_descriptor b_to_c =
      add_edge(b_h, c_h, {"b->c"}, graph).first;
  const Graph::edge_descriptor b_to_s =
      add_edge(b_c, s_h, {"b->s"}, graph).first;
  const Graph::edge_descriptor b_to_f =
      add_edge(b_h, f_h, {"b->f"}, graph).first;
  const Graph::edge_descriptor c_to_d =
      add_edge(c_h, d_h, {"c->d"}, graph).first;
  const Graph::edge_descriptor d_to_e =
      add_edge(d_c, e_h, {"d->e"}, graph).first;
  const Graph::edge_descriptor e_to_f =
      add_edge(e_h, f_h, {"e->f"}, graph).first;
  const Graph::edge_descriptor main_to_a =
      add_edge(main, a_h, {"main->a"}, graph).first;
  const Graph::edge_descriptor main_to_e =
      add_edge(main, e_h, {"main->e"}, graph).first;

  graph[a_h].component = a_c;
  graph[a_c].component = a_h;
  graph[b_h].component = b_c;
  graph[b_c].component = b_h;
  graph[c_h].component = c_c;
  graph[c_c].component = c_h;
  graph[d_h].component = d_c;
  graph[d_c].component = d_h;

  std::vector<find_unnecessary_sources::result> actual =
      find_unnecessary_sources::from_graph(graph, {main, a_c, b_c, c_c, d_c},
                                           INT_MIN);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<find_unnecessary_sources::result> expected = {
      {a_c, A_C + A_H + B_H + C_H + D_H + F_H, A_C},
      {b_c, B_C + S_H + B_H + C_H + D_H + F_H, 2 * (B_C + S_H)},
      {c_c, C_C + C_H + D_H, 3 * C_C},
      {d_c, D_C + D_H + E_H + F_H, D_C + 2 * (D_C + E_H) + (D_C + E_H + F_H)},
  };
  EXPECT_EQ(actual, expected);
}

} // namespace
