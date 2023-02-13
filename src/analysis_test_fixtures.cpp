#include "analysis_test_fixtures.hpp"

namespace IncludeGuardian {

using namespace boost::units::information;

DiamondGraph::DiamondGraph()
    : graph(), A(1, 2000000000.0 * bytes), B(10, 200000000.0 * bytes),
      C(100, 20000000.0 * bytes), D(1000, 2000000.0 * bytes),
      a(add_vertex(file_node("a").with_cost(A), graph)),
      b(add_vertex(file_node("b").with_cost(B).set_internal_parents(1), graph)),
      c(add_vertex(file_node("c").with_cost(C).set_internal_parents(1), graph)),
      d(add_vertex(file_node("d").with_cost(D).set_internal_parents(2), graph)),
      a_to_b(add_edge(a, b, {"a->b"}, graph).first),
      a_to_c(add_edge(a, c, {"a->c"}, graph).first),
      b_to_d(add_edge(b, d, {"b->d"}, graph).first),
      c_to_d(add_edge(c, d, {"c->d"}, graph).first) {}

std::span<const Graph::vertex_descriptor> DiamondGraph::sources() const {
  return {&a, 1};
}

MultiLevel::MultiLevel()
    : graph(), A(1, 2000000000.0 * bytes), B(10, 200000000.0 * bytes),
      C(100, 20000000.0 * bytes), D(1000, 2000000.0 * bytes),
      E(10000, 200000.0 * bytes), F(100000, 20000.0 * bytes),
      G(1000000, 2000.0 * bytes), H(10000000, 200.0 * bytes),
      a(add_vertex(file_node("a").with_cost(A), graph)),
      b(add_vertex(file_node("b").with_cost(B), graph)),
      c(add_vertex(file_node("c").with_cost(C).set_internal_parents(1), graph)),
      d(add_vertex(file_node("d").with_cost(D).set_internal_parents(2), graph)),
      e(add_vertex(file_node("e").with_cost(E).set_internal_parents(1), graph)),
      f(add_vertex(file_node("f").with_cost(F).set_internal_parents(2), graph)),
      g(add_vertex(file_node("g").with_cost(G).set_internal_parents(1), graph)),
      h(add_vertex(file_node("h").with_cost(H).set_internal_parents(2), graph)),
      a_to_c(add_edge(a, c, {"a->c"}, graph).first),
      a_to_d(add_edge(a, d, {"a->d"}, graph).first),
      b_to_d(add_edge(b, d, {"b->d"}, graph).first),
      b_to_e(add_edge(b, e, {"b->e"}, graph).first),
      c_to_f(add_edge(c, f, {"c->f"}, graph).first),
      d_to_f(add_edge(d, f, {"d->f"}, graph).first),
      e_to_g(add_edge(e, g, {"e->g"}, graph).first),
      e_to_h(add_edge(e, h, {"e->h"}, graph).first),
      f_to_h(add_edge(f, h, {"f->h"}, graph).first),
      g_to_h(add_edge(g, h, {"g->h"}, graph).first), sources_arr{a, b} {}

std::span<const Graph::vertex_descriptor> MultiLevel::sources() const {
  return sources_arr;
}

LongChain::LongChain()
    : graph(), A(1, 2000000000.0 * bytes), B(10, 200000000.0 * bytes),
      C(100, 20000000.0 * bytes), D(1000, 2000000.0 * bytes),
      E(10000, 200000.0 * bytes), F(100000, 20000.0 * bytes),
      G(1000000, 2000.0 * bytes), H(10000000, 200.0 * bytes),
      I(100000000, 20.0 * bytes), J(1000000000, 2.0 * bytes),
      a(add_vertex(file_node("a").with_cost(A), graph)),
      b(add_vertex(file_node("b").with_cost(B).set_internal_parents(1), graph)),
      c(add_vertex(file_node("c").with_cost(C).set_internal_parents(1), graph)),
      d(add_vertex(file_node("d").with_cost(D).set_internal_parents(2), graph)),
      e(add_vertex(file_node("e").with_cost(E).set_internal_parents(1), graph)),
      f(add_vertex(file_node("f").with_cost(F).set_internal_parents(1), graph)),
      g(add_vertex(file_node("g").with_cost(G).set_internal_parents(2), graph)),
      h(add_vertex(file_node("h").with_cost(H).set_internal_parents(1), graph)),
      i(add_vertex(file_node("i").with_cost(I).set_internal_parents(2), graph)),
      j(add_vertex(file_node("j").with_cost(J).set_internal_parents(2), graph)),

      a_to_b(add_edge(a, b, {"a->b"}, graph).first),
      a_to_c(add_edge(a, c, {"a->c"}, graph).first),
      b_to_d(add_edge(b, d, {"b->d"}, graph).first),
      c_to_d(add_edge(c, d, {"c->d"}, graph).first),
      d_to_e(add_edge(d, e, {"d->e"}, graph).first),
      d_to_f(add_edge(d, f, {"d->f"}, graph).first),
      e_to_g(add_edge(e, g, {"e->g"}, graph).first),
      f_to_g(add_edge(f, g, {"f->g"}, graph).first),
      f_to_i(add_edge(f, i, {"f->i"}, graph).first),
      g_to_h(add_edge(g, h, {"g->h"}, graph).first),
      g_to_i(add_edge(g, i, {"g->i"}, graph).first),
      h_to_j(add_edge(h, j, {"h->j"}, graph).first),
      i_to_j(add_edge(i, j, {"i->j"}, graph).first) {}

std::span<const Graph::vertex_descriptor> LongChain::sources() const {
  return {&a, 1};
}

WInclude::WInclude()
    : graph(), A_H(1, 20000000000.0 * bytes), A_C(10, 2000000000.0 * bytes),
      B_H(100, 200000000.0 * bytes), B_C(1000, 20000000.0 * bytes),
      MAIN_C(12345, 98765.0 * bytes),
      a_h(add_vertex(file_node("a.h").with_cost(A_H).set_internal_parents(2),
                     graph)),
      a_c(add_vertex(file_node("a.c").with_cost(A_C), graph)),
      b_h(add_vertex(file_node("b.h").with_cost(B_H).set_internal_parents(2),
                     graph)),
      b_c(add_vertex(file_node("b.c").with_cost(B_C), graph)),
      main_c(add_vertex(file_node("main.c").with_cost(MAIN_C), graph)),
      a_link(add_edge(a_c, a_h, {"a->a"}, graph).first),
      b_link(add_edge(b_c, b_h, {"b->b"}, graph).first),
      main_to_a(add_edge(main_c, a_h, {"main->a"}, graph).first),
      main_to_b(add_edge(main_c, b_h, {"main->b"}, graph).first), sources_arr{
                                                                      a_c, b_c,
                                                                      main_c} {
  graph[a_h].component = a_c;
  graph[a_c].component = a_h;
  graph[b_h].component = b_c;
  graph[b_c].component = b_h;
}

std::span<const Graph::vertex_descriptor> WInclude::sources() const {
  return sources_arr;
}

CascadingInclude::CascadingInclude()
    : graph(), A_H(1, 20000000000.0 * bytes), A_C(10, 2000000000.0 * bytes),
      B_H(100, 200000000.0 * bytes), B_C(1000, 20000000.0 * bytes),
      C_H(10000, 2000000.0 * bytes), C_C(100000, 200000.0 * bytes),
      D_H(1000000, 20000.0 * bytes), D_C(10000000, 2000.0 * bytes),
      MAIN_C(12345, 98765.0 * bytes),
      a_h(add_vertex(file_node("a.h").with_cost(A_H).set_internal_parents(2),
                     graph)),
      a_c(add_vertex(file_node("a.c").with_cost(A_C), graph)),
      b_h(add_vertex(file_node("b.h").with_cost(B_H).set_internal_parents(3),
                     graph)),
      b_c(add_vertex(file_node("b.c").with_cost(B_C), graph)),
      c_h(add_vertex(file_node("c.h").with_cost(C_H).set_internal_parents(3),
                     graph)),
      c_c(add_vertex(file_node("c.c").with_cost(C_C), graph)),
      d_h(add_vertex(file_node("d.h").with_cost(D_H).set_internal_parents(3),
                     graph)),
      d_c(add_vertex(file_node("d.c").with_cost(D_C), graph)),
      main_c(add_vertex(file_node("main.c").with_cost(MAIN_C), graph)),
      a_link(add_edge(a_c, a_h, {"a->a"}, graph).first),
      b_link(add_edge(b_c, b_h, {"b->b"}, graph).first),
      c_link(add_edge(c_c, c_h, {"c->c"}, graph).first),
      d_link(add_edge(d_c, d_h, {"d->d"}, graph).first),
      a_to_b(add_edge(a_h, b_h, {"a->b"}, graph).first),
      b_to_c(add_edge(b_h, c_h, {"b->c"}, graph).first),
      c_to_d(add_edge(c_h, d_h, {"c->d"}, graph).first),
      main_to_a(add_edge(main_c, a_h, {"main->a"}, graph).first),
      sources_arr{main_c, a_c, b_c, c_c, d_c} {
  graph[a_h].component = a_c;
  graph[a_c].component = a_h;
  graph[b_h].component = b_c;
  graph[b_c].component = b_h;
  graph[c_h].component = c_c;
  graph[c_c].component = c_h;
  graph[d_h].component = d_c;
  graph[d_c].component = d_h;
}

std::span<const Graph::vertex_descriptor> CascadingInclude::sources() const {
  return sources_arr;
}

ComplexCascadingInclude::ComplexCascadingInclude()
    : graph(), A_H(1, 20000000000.0 * bytes), A_C(10, 2000000000.0 * bytes),
      B_H(100, 200000000.0 * bytes), B_C(1000, 20000000.0 * bytes),
      C_H(10000, 2000000.0 * bytes), C_C(100000, 200000.0 * bytes),
      D_H(1000000, 20000.0 * bytes), D_C(10000000, 2000.0 * bytes),
      E_H(100000000, 200.0 * bytes), F_H(1000000000, 20.0 * bytes),
      S_H(99, 2.0 * bytes), MAIN_C(12345, 98765.0 * bytes),
      a_h(add_vertex(file_node("a.h").with_cost(A_H).set_internal_parents(2),
                     graph)),
      a_c(add_vertex(file_node("a.c").with_cost(A_C), graph)),
      b_h(add_vertex(file_node("b.h").with_cost(B_H).set_internal_parents(3),
                     graph)),
      b_c(add_vertex(file_node("b.c").with_cost(B_C), graph)),
      c_h(add_vertex(file_node("c.h").with_cost(C_H).set_internal_parents(3),
                     graph)),
      c_c(add_vertex(file_node("c.c").with_cost(C_C), graph)),
      d_h(add_vertex(file_node("d.h").with_cost(D_H).set_internal_parents(3),
                     graph)),
      d_c(add_vertex(file_node("d.c").with_cost(D_C), graph)),
      e_h(add_vertex(file_node("e.h").with_cost(E_H).set_internal_parents(1),
                     graph)),
      f_h(add_vertex(file_node("f.h").with_cost(F_H).set_internal_parents(1),
                     graph)),
      s_h(add_vertex(file_node("s.h").with_cost(S_H).set_internal_parents(1),
                     graph)),
      main_c(add_vertex(file_node("main.c").with_cost(MAIN_C), graph)),
      a_link(add_edge(a_c, a_h, {"a->a"}, graph).first),
      b_link(add_edge(b_c, b_h, {"b->b"}, graph).first),
      c_link(add_edge(c_c, c_h, {"c->c"}, graph).first),
      d_link(add_edge(d_c, d_h, {"d->d"}, graph).first),
      a_to_b(add_edge(a_h, b_h, {"a->b"}, graph).first),
      b_to_c(add_edge(b_h, c_h, {"b->c"}, graph).first),
      b_to_f(add_edge(b_h, f_h, {"b->f"}, graph).first),
      b_to_s(add_edge(b_c, s_h, {"b->s"}, graph).first),
      c_to_d(add_edge(c_h, d_h, {"c->d"}, graph).first),
      d_to_e(add_edge(d_c, e_h, {"d->e"}, graph).first),
      e_to_f(add_edge(e_h, f_h, {"e->f"}, graph).first),
      main_to_a(add_edge(main_c, a_h, {"main->a"}, graph).first),
      main_to_e(add_edge(main_c, e_h, {"main->e"}, graph).first),
      sources_arr{main_c, a_c, b_c, c_c, d_c} {
  graph[a_h].component = a_c;
  graph[a_c].component = a_h;
  graph[b_h].component = b_c;
  graph[b_c].component = b_h;
  graph[c_h].component = c_c;
  graph[c_c].component = c_h;
  graph[d_h].component = d_c;
  graph[d_c].component = d_h;
}

std::span<const Graph::vertex_descriptor>
ComplexCascadingInclude::sources() const {
  return sources_arr;
}

std::span<const Graph::vertex_descriptor> NoSources::sources() const {
  return {};
}

} // namespace IncludeGuardian
