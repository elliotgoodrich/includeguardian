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
      g_to_h(add_edge(g, h, {"g->h"}, graph).first) {}

std::span<const Graph::vertex_descriptor> MultiLevel::sources() const {
  static const Graph::vertex_descriptor s[] = {a, b};
  return s;
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

} // namespace IncludeGuardian

