#include "find_expensive_headers.hpp"

#include "analysis_test_fixtures.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

bool test_sort(const find_expensive_headers::result &lhs,
               const find_expensive_headers::result &rhs) {
  return lhs.v < rhs.v;
}

TEST_F(DiamondGraph, FindExpensiveHeaders) {
  std::vector<find_expensive_headers::result> actual =
      find_expensive_headers::from_graph(graph, sources(), INT_MIN);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<find_expensive_headers::result> expected = {
      {b, cost{}, B + D},
      {c, cost{}, C + D},
      {d, D, D},
  };
  EXPECT_EQ(actual, expected);
}

TEST_F(MultiLevel, FindExpensiveHeaders) {
  std::vector<find_expensive_headers::result> actual =
      find_expensive_headers::from_graph(graph, sources(), INT_MIN);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<find_expensive_headers::result> expected = {
      {c, cost{}, C + F + H}, {d, cost{}, D + F + H}, {e, cost{}, E + G + H},
      {f, F + F + H, F + H},  {g, G, G + H},          {h, H + H, H},
  };
  EXPECT_EQ(actual, expected);
}

TEST_F(LongChain, FindExpensiveHeaders) {
  std::vector<find_expensive_headers::result> actual =
      find_expensive_headers::from_graph(graph, sources(), INT_MIN);
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<find_expensive_headers::result> expected = {
      {b, cost{}, B + D + E + F + G + H + I + J},
      {c, cost{}, C + D + E + F + G + H + I + J},
      {d, D + E + F + G + H + I + J, D + E + F + G + H + I + J},
      {e, E, E + G + H + I + J},
      {f, F, F + G + H + I + J},
      {g, G + H, G + H + I + J},
      {h, H, H + J},
      {i, I, I + J},
      {j, J, J},
  };
  EXPECT_EQ(actual, expected);
}

} // namespace
