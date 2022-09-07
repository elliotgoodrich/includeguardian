#include "find_expensive_includes.hpp"

#include "analysis_test_fixtures.hpp"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

using namespace IncludeGuardian;
using namespace testing;

namespace {

TEST_F(DiamondGraph, FindExpensiveIncludes) {
  EXPECT_THAT(find_expensive_includes::from_graph(graph, sources(), 1u),
              UnorderedElementsAreArray({
                  include_directive_and_cost{"a", B, &graph[a_to_b]},
                  include_directive_and_cost{"a", C, &graph[a_to_c]},
              }));
}

TEST_F(MultiLevel, FindExpensiveIncludes) {
  EXPECT_THAT(find_expensive_includes::from_graph(graph, sources(), 1u),
              UnorderedElementsAreArray({
                  include_directive_and_cost{"a", C, &graph[a_to_c]},
                  include_directive_and_cost{"a", D, &graph[a_to_d]},
                  include_directive_and_cost{"b", D + F, &graph[b_to_d]},
                  include_directive_and_cost{"b", E + G, &graph[b_to_e]},
                  include_directive_and_cost{"d", F, &graph[d_to_f]},
                  include_directive_and_cost{"e", G, &graph[e_to_g]},
                  include_directive_and_cost{"f", H, &graph[f_to_h]},
              }));
}

TEST_F(LongChain, FindExpensiveIncludes) {
  EXPECT_THAT(find_expensive_includes::from_graph(graph, sources(), 1u),
              UnorderedElementsAreArray({
                  include_directive_and_cost{"a", B, &graph[a_to_b]},
                  include_directive_and_cost{"a", C, &graph[a_to_c]},
                  include_directive_and_cost{"d", E, &graph[d_to_e]},
                  include_directive_and_cost{"d", F, &graph[d_to_f]},
                  include_directive_and_cost{"g", H, &graph[g_to_h]},
              }));
}

} // namespace
