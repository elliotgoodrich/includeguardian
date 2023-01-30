#include "find_expensive_headers.hpp"

#include "analysis_test_fixtures.hpp"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

using namespace IncludeGuardian;
using namespace testing;

namespace {

using result = find_expensive_headers::result;

TEST_F(DiamondGraph, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT64_MIN),
              UnorderedElementsAreArray({
                  result{b, cost{}, 0},
                  result{c, cost{}, 0},
                  result{d, cost{-B - C - D}, 2},
              }));
}

TEST_F(MultiLevel, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT64_MIN),
              UnorderedElementsAreArray({
                  result{c, cost{}, 0},
                  result{d, cost{}, 0},
                  result{e, cost{}, 0},
                  result{f, cost{-C - D - H}, 2},
                  result{g, -E - H, 1},
                  result{h, -E - F - 2 * G - H, 3},
              }));
}

TEST_F(LongChain, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT64_MIN),
              UnorderedElementsAreArray({
                  result{b, cost{}, 0},
                  result{c, cost{}, 0},
                  result{d, -B - C - D - E - F - G - H - I - J, 2},
                  result{e, -D - F - G - H - I - J, 1},
                  result{f, -D - E - G - H - I - J, 1},
                  result{g, -E - F - G - H - 2 * I - 2 * J, 2},
                  result{h, -G - I - J, 1},
                  result{i, -F - 2 * G - 2 * H - I - 2 * J, 2},
                  result{j, -H - I - J, 2},
              }));
}

TEST_F(WInclude, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT64_MIN),
              UnorderedElementsAreArray({
                  result{a_h, cost{}, 0},
                  result{b_h, cost{}, 0},
              }));
}

TEST_F(CascadingInclude, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT64_MIN),
              UnorderedElementsAreArray({
                  result{a_h, cost{}, 0},
                  result{b_h, cost{B_H + C_H + D_H}, 1},
                  result{c_h, cost{2 * (C_H + D_H)}, 1},
                  result{d_h, cost{3 * D_H}, 1},
              }));
}

TEST_F(ComplexCascadingInclude, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT64_MIN),
              UnorderedElementsAreArray({
                  result{a_h, cost{}, 0},
                  result{b_h, cost{B_H + C_H + D_H}, 1},
                  result{c_h, cost{2 * (C_H + D_H)}, 1},
                  result{d_h, cost{3 * D_H}, 1},
                  result{e_h, cost{}, 0},
                  result{f_h, cost{2 * F_H - E_H}, 2},
                  result{s_h, cost{}, 0},
              }));
}

} // namespace
