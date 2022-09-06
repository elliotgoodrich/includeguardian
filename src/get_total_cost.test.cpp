#include "get_total_cost.hpp"

#include "analysis_test_fixtures.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

TEST_F(DiamondGraph, GetTotalCost) {
  EXPECT_EQ(get_total_cost::from_graph(graph, sources()).true_cost, A + B + C + D);
}

TEST_F(MultiLevel, GetTotalCost) {
  EXPECT_EQ(get_total_cost::from_graph(graph, sources()).true_cost,
            (A + C + D + F + H) + (B + D + E + F + G + H));
}

TEST_F(LongChain, GetTotalCostTest) {
  EXPECT_EQ(get_total_cost::from_graph(graph, sources()).true_cost,
            A + B + C + D + E + F + G + H + I + J);
}

} // namespace
