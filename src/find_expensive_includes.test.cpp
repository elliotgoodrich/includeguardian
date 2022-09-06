#include "find_expensive_includes.hpp"

#include "analysis_test_fixtures.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

bool test_sort(const include_directive_and_cost &lhs,
               const include_directive_and_cost &rhs) {
  return std::tie(lhs.file, lhs.include->code, lhs.saving.token_count) <
         std::tie(rhs.file, rhs.include->code, rhs.saving.token_count);
}

TEST_F(DiamondGraph, FindExpensiveIncludes) {
  std::vector<include_directive_and_cost> actual =
      find_expensive_includes::from_graph(graph, sources(), 1u);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<include_directive_and_cost> expected = {
      {"a", B, &graph[a_to_b]},
      {"a", C, &graph[a_to_c]},
  };
  EXPECT_EQ(actual, expected);
}

TEST_F(MultiLevel, FindExpensiveIncludes) {
  std::vector<include_directive_and_cost> actual =
      find_expensive_includes::from_graph(graph, sources(), 1u);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<include_directive_and_cost> expected = {
      {"a", C, &graph[a_to_c]},     {"a", D, &graph[a_to_d]},
      {"b", D + F, &graph[b_to_d]}, {"b", E + G, &graph[b_to_e]},
      {"d", F, &graph[d_to_f]},     {"e", G, &graph[e_to_g]},
      {"f", H, &graph[f_to_h]},
  };
  EXPECT_EQ(actual, expected);
}

TEST_F(LongChain, FindExpensiveIncludes) {
  std::vector<include_directive_and_cost> actual =
      find_expensive_includes::from_graph(graph, sources(), 1u);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<include_directive_and_cost> expected = {
      {"a", B, &graph[a_to_b]}, {"a", C, &graph[a_to_c]},
      {"d", E, &graph[d_to_e]}, {"d", F, &graph[d_to_f]},
      {"g", H, &graph[g_to_h]},
  };
  EXPECT_EQ(actual, expected);
}

} // namespace
