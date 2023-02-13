#include "find_unnecessary_sources.hpp"

#include "analysis_test_fixtures.hpp"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

namespace {

using namespace IncludeGuardian;
using namespace testing;

using result = find_unnecessary_sources::result;

TEST_F(WInclude, FindUnnecessarySourcesTest) {
  EXPECT_THAT(find_unnecessary_sources::from_graph(graph, sources(), INT_MIN),
              UnorderedElementsAreArray({
                  result{a_c, A_C + A_H, A_C},
                  result{b_c, B_C + B_H, B_C},
              }));
}

TEST_F(CascadingInclude, FindUnnecessarySourcesTest) {
  EXPECT_THAT(find_unnecessary_sources::from_graph(graph, sources(), INT_MIN),
              UnorderedElementsAreArray({
                  result{a_c, A_C + A_H + B_H + C_H + D_H, A_C},
                  result{b_c, B_C + B_H + C_H + D_H, 2 * B_C},
                  result{c_c, C_C + C_H + D_H, 3 * C_C},
                  result{d_c, D_C + D_H, 4 * D_C},
              }));
}

TEST_F(ComplexCascadingInclude, FindUnnecessarySourcesTest) {
  EXPECT_THAT(
      find_unnecessary_sources::from_graph(graph, sources(), INT_MIN),
      UnorderedElementsAreArray({
          result{a_c, A_C + A_H + B_H + C_H + D_H + F_H, A_C},
          result{b_c, B_C + S_H + B_H + C_H + D_H + F_H, 2 * (B_C + S_H)},
          result{c_c, C_C + C_H + D_H, 3 * C_C},
          result{d_c, D_C + D_H + E_H + F_H,
                 D_C + 2 * (D_C + E_H) + (D_C + E_H + F_H)},
      }));
}

TEST_F(NoSources, FindUnnecessarySourcesTest) {
  EXPECT_THAT(find_unnecessary_sources::from_graph(graph, sources(), INT_MIN),
              SizeIs(0));
}

} // namespace
