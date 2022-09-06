#include "find_expensive_files.hpp"

#include "analysis_test_fixtures.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

bool test_sort(const file_and_cost &lhs, const file_and_cost &rhs) {
  return lhs.node->path < rhs.node->path;
}

TEST_F(DiamondGraph, FindExpensiveFiles) {
  std::vector<file_and_cost> actual =
      find_expensive_files::from_graph(graph, sources(), 1u);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<file_and_cost> expected = {
      {&graph[a], 1},
      {&graph[b], 1},
      {&graph[c], 1},
      {&graph[d], 1},
  };
  EXPECT_EQ(actual, expected);
}

TEST_F(MultiLevel, FindExpensiveFiles) {
  std::vector<file_and_cost> actual =
      find_expensive_files::from_graph(graph, sources(), 1u);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<file_and_cost> expected = {
      {&graph[a], 1}, {&graph[b], 1}, {&graph[c], 1}, {&graph[d], 2},
      {&graph[e], 1}, {&graph[f], 2}, {&graph[g], 1}, {&graph[h], 2},
  };
  EXPECT_EQ(actual, expected);
}

TEST_F(LongChain, FindExpensiveFiles) {
  std::vector<file_and_cost> actual =
      find_expensive_files::from_graph(graph, sources(), 1u);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<file_and_cost> expected = {
      {&graph[a], 1}, {&graph[b], 1}, {&graph[c], 1}, {&graph[d], 1},
      {&graph[e], 1}, {&graph[f], 1}, {&graph[g], 1}, {&graph[h], 1},
      {&graph[i], 1}, {&graph[j], 1},
  };
  EXPECT_EQ(actual, expected);
}

} // namespace
