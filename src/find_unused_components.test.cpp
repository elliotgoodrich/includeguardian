#include "find_unused_components.hpp"

#include "analysis_test_fixtures.hpp"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

using namespace IncludeGuardian;
using namespace testing;

namespace {

TEST_F(WInclude, FindUnusedComponentsTest) {
  EXPECT_THAT(find_unused_components::from_graph(graph, sources(), 0u),
              SizeIs(0));
  EXPECT_THAT(find_unused_components::from_graph(graph, sources(), 1u),
              UnorderedElementsAreArray({
                  component_and_cost{&graph[a_c]},
                  component_and_cost{&graph[b_c]},
              }));
}

TEST_F(CascadingInclude, FindUnusedComponentsTest) {
  EXPECT_THAT(find_unused_components::from_graph(graph, sources(), 0u),
              SizeIs(0));
  EXPECT_THAT(find_unused_components::from_graph(graph, sources(), 1u),
              UnorderedElementsAreArray({
                  component_and_cost{&graph[a_c]},
                  component_and_cost{&graph[b_c]},
                  component_and_cost{&graph[c_c]},
                  component_and_cost{&graph[d_c]},
              }));
}

TEST_F(ComplexCascadingInclude, FindUnusedComponentsTest) {
  EXPECT_THAT(find_unused_components::from_graph(graph, sources(), 0u),
              SizeIs(0));
  EXPECT_THAT(find_unused_components::from_graph(graph, sources(), 1u),
              UnorderedElementsAreArray({
                  component_and_cost{&graph[a_c]},
                  component_and_cost{&graph[b_c]},
                  component_and_cost{&graph[c_c]},
                  component_and_cost{&graph[d_c]},
              }));
}

TEST_F(NoSources, FindUnusedComponentsTest) {
  EXPECT_THAT(find_unused_components::from_graph(graph, sources(), 0u),
              SizeIs(0));
}

} // namespace
