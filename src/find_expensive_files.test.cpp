#include "find_expensive_files.hpp"

#include "analysis_test_fixtures.hpp"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

using namespace IncludeGuardian;
using namespace testing;

namespace {

TEST_F(DiamondGraph, FindExpensiveFiles) {
  EXPECT_THAT(find_expensive_files::from_graph(graph, sources(), 1u),
              UnorderedElementsAreArray({
                  file_and_cost{&graph[a], 1},
                  file_and_cost{&graph[b], 1},
                  file_and_cost{&graph[c], 1},
                  file_and_cost{&graph[d], 1},
              }));
}

TEST_F(MultiLevel, FindExpensiveFiles) {
  EXPECT_THAT(find_expensive_files::from_graph(graph, sources(), 1u),
              UnorderedElementsAreArray({
                  file_and_cost{&graph[a], 1},
                  file_and_cost{&graph[b], 1},
                  file_and_cost{&graph[c], 1},
                  file_and_cost{&graph[d], 2},
                  file_and_cost{&graph[e], 1},
                  file_and_cost{&graph[f], 2},
                  file_and_cost{&graph[g], 1},
                  file_and_cost{&graph[h], 2},
              }));
}

TEST_F(LongChain, FindExpensiveFiles) {
  EXPECT_THAT(find_expensive_files::from_graph(graph, sources(), 1u),
              UnorderedElementsAreArray({
                  file_and_cost{&graph[a], 1},
                  file_and_cost{&graph[b], 1},
                  file_and_cost{&graph[c], 1},
                  file_and_cost{&graph[d], 1},
                  file_and_cost{&graph[e], 1},
                  file_and_cost{&graph[f], 1},
                  file_and_cost{&graph[g], 1},
                  file_and_cost{&graph[h], 1},
                  file_and_cost{&graph[i], 1},
                  file_and_cost{&graph[j], 1},
              }));
}

TEST_F(NoSources, FindExpensiveFiles) {
  EXPECT_THAT(find_expensive_files::from_graph(graph, sources(), 1u),
              SizeIs(0));
}

} // namespace
