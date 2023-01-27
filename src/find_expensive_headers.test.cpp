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
                  result{b, cost{}},
                  result{c, cost{}},
                  result{d, cost{-B - C - D}},
              }));
}

TEST_F(MultiLevel, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT64_MIN),
              UnorderedElementsAreArray({
                  result{c, cost{}},
                  result{d, cost{}},
                  result{e, cost{}},
                  result{f, cost{-C - D - H}},
                  result{g, -E - H},
                  result{h, -E - F - 2 * G - H},
              }));
}

TEST_F(LongChain, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT64_MIN),
              UnorderedElementsAreArray({
                  result{b, cost{}},
                  result{c, cost{}},
                  result{d, -B - C - D - E - F - G - H - I - J},
                  result{e, -D - F - G - H - I - J},
                  result{f, -D - E - G - H - I - J},
                  result{g, -E - F - G - H - 2 * I - 2 * J},
                  result{h, -G - I - J},
                  result{i, -F - 2 * G - 2 * H - I - 2 * J},
                  result{j, -H - I - J},
              }));
}

TEST_F(WInclude, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT64_MIN),
              UnorderedElementsAreArray({
                  result{a_h, cost{}},
                  result{b_h, cost{}},
              }));
}

TEST_F(CascadingInclude, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT64_MIN),
              UnorderedElementsAreArray({
                  result{a_h, cost{}},
                  result{b_h, cost{B_H + C_H + D_H}},
                  result{c_h, cost{2 * (C_H + D_H)}},
                  result{d_h, cost{3 * D_H}},
              }));
}

TEST_F(ComplexCascadingInclude, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT64_MIN),
              UnorderedElementsAreArray({
                  result{a_h, cost{}},
                  result{b_h, cost{B_H + C_H + D_H}},
                  result{c_h, cost{2 * (C_H + D_H)}},
                  result{d_h, cost{3 * D_H}},
                  result{e_h, cost{}},
                  result{f_h, cost{2 * F_H - E_H}},
                  result{s_h, cost{}},
              }));
}

} // namespace
