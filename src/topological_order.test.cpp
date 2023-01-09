#include "topological_order.hpp"

#include "analysis_test_fixtures.hpp"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

using namespace IncludeGuardian;
using namespace testing;

namespace {

TEST_F(DiamondGraph, TopologicalOrder) {
  EXPECT_THAT(topological_order::from_graph(graph, sources()),
              ElementsAre(ElementsAre(ElementsAre(d)),
                          ElementsAre(ElementsAre(b), ElementsAre(c)),
                          ElementsAre(ElementsAre(a))));
}

TEST_F(MultiLevel, TopologicalOrder) {
  EXPECT_THAT(
      topological_order::from_graph(graph, sources()),
      ElementsAre(ElementsAre(ElementsAre(h)),
                  ElementsAre(ElementsAre(f), ElementsAre(g)),
                  ElementsAre(ElementsAre(c), ElementsAre(d), ElementsAre(e)),
                  ElementsAre(ElementsAre(a), ElementsAre(b))));
}

TEST_F(LongChain, TopologicalOrder) {
  EXPECT_THAT(topological_order::from_graph(graph, sources()),
      ElementsAre(ElementsAre(ElementsAre(j)),
                  ElementsAre(ElementsAre(h), ElementsAre(i)),
                  ElementsAre(ElementsAre(g)),
                  ElementsAre(ElementsAre(e), ElementsAre(f)),
                  ElementsAre(ElementsAre(d)),
                  ElementsAre(ElementsAre(b), ElementsAre(c)),
                  ElementsAre(ElementsAre(a))));
}

TEST_F(WInclude, TopologicalOrder) {
  EXPECT_THAT(topological_order::from_graph(graph, sources()),
      ElementsAre(ElementsAre(ElementsAre(a_h, a_c), ElementsAre(b_h, b_c)),
                  ElementsAre(ElementsAre(main_c))));
}

TEST_F(CascadingInclude, TopologicalOrder) {
  EXPECT_THAT(topological_order::from_graph(graph, sources()),
      ElementsAre(ElementsAre(ElementsAre(d_h, d_c)),
                  ElementsAre(ElementsAre(c_h, c_c)),
                  ElementsAre(ElementsAre(b_h, b_c)),
                  ElementsAre(ElementsAre(a_h, a_c)),
                  ElementsAre(ElementsAre(main_c))));
}

TEST_F(ComplexCascadingInclude, TopologicalOrder) {
  EXPECT_THAT(topological_order::from_graph(graph, sources()),
      ElementsAre(ElementsAre(ElementsAre(f_h), ElementsAre(s_h)),
                  ElementsAre(ElementsAre(e_h)),
                  ElementsAre(ElementsAre(d_h, d_c)),
                  ElementsAre(ElementsAre(c_h, c_c)),
                  ElementsAre(ElementsAre(b_h, b_c)),
                  ElementsAre(ElementsAre(a_h, a_c)),
                  ElementsAre(ElementsAre(main_c))));
}

} // namespace
