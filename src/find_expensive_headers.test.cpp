#include "find_expensive_headers.hpp"

#include "analysis_test_fixtures.hpp"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

using namespace IncludeGuardian;
using namespace testing;

namespace {

using result = find_expensive_headers::result;

TEST_F(DiamondGraph, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT_MIN),
              UnorderedElementsAreArray({
                  result{b, cost{}, B + D},
                  result{c, cost{}, C + D},
                  result{d, D, D},
              }));
}

TEST_F(MultiLevel, FindExpensiveHeaders) {
  EXPECT_THAT(find_expensive_headers::from_graph(graph, sources(), INT_MIN),
              UnorderedElementsAreArray({
                  result{c, cost{}, C + F + H},
                  result{d, cost{}, D + F + H},
                  result{e, cost{}, E + G + H},
                  result{f, F + F + H, F + H},
                  result{g, G, G + H},
                  result{h, H + H, H},
              }));
}

TEST_F(LongChain, FindExpensiveHeaders) {
  EXPECT_THAT(
      find_expensive_headers::from_graph(graph, sources(), INT_MIN),
      UnorderedElementsAreArray({
          result{b, cost{}, B + D + E + F + G + H + I + J},
          result{c, cost{}, C + D + E + F + G + H + I + J},
          result{d, D + E + F + G + H + I + J, D + E + F + G + H + I + J},
          result{e, E, E + G + H + I + J},
          result{f, F, F + G + H + I + J},
          result{g, G + H, G + H + I + J},
          result{h, H, H + J},
          result{i, I, I + J},
          result{j, J, J},
      }));
}

} // namespace
