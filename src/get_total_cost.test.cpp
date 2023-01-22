#include "get_total_cost.hpp"

#include "analysis_test_fixtures.hpp"

#include <gtest/gtest.h>

using namespace IncludeGuardian;

namespace {

TEST_F(DiamondGraph, GetTotalCost) {
  EXPECT_EQ(get_total_cost::from_graph(graph, sources()).true_cost,
            A + B + C + D);
}

TEST_F(MultiLevel, GetTotalCost) {
  EXPECT_EQ(get_total_cost::from_graph(graph, sources()).true_cost,
            (A + C + D + F + H) + (B + D + E + F + G + H));
}

TEST_F(LongChain, GetTotalCostTest) {
  EXPECT_EQ(get_total_cost::from_graph(graph, sources()).true_cost,
            A + B + C + D + E + F + G + H + I + J);
}

TEST_F(WInclude, GetTotalCostTest) {
  EXPECT_EQ(get_total_cost::from_graph(graph, sources()).true_cost,
            (2 * A_H) + A_C + (2 * B_H) + B_C + MAIN_C);
}

TEST_F(CascadingInclude, GetTotalCostTest) {
  EXPECT_EQ(get_total_cost::from_graph(graph, sources()).true_cost,
            (2 * A_H) + A_C + (3 * B_H) + B_C + (4 * C_H) + C_C + (5 * D_H) +
                D_C + MAIN_C);
}

TEST_F(ComplexCascadingInclude, GetTotalCostTest) {
  EXPECT_EQ(get_total_cost::from_graph(graph, sources()).true_cost,
            (2 * A_H) + A_C + (3 * B_H) + B_C + (4 * C_H) + C_C + (5 * D_H) +
                D_C + (2 * E_H) + (4 * F_H) + S_H + MAIN_C);
}

} // namespace
