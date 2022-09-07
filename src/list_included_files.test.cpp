#include "list_included_files.hpp"

#include "analysis_test_fixtures.hpp"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

using namespace IncludeGuardian;
using namespace testing;

namespace {

using result = list_included_files::result;

TEST_F(DiamondGraph, ListIncludedFiles) {
  EXPECT_THAT(list_included_files::from_graph(graph, sources()),
              UnorderedElementsAreArray({
                  result{a, 1u},
                  result{b, 1u},
                  result{c, 1u},
                  result{d, 1u},
              }));
}

TEST_F(MultiLevel, ListIncludedFiles) {
  EXPECT_THAT(list_included_files::from_graph(graph, sources()),
              UnorderedElementsAreArray({
                  result{a, 1u},
                  result{b, 1u},
                  result{c, 1u},
                  result{d, 2u},
                  result{e, 1u},
                  result{f, 2u},
                  result{g, 1u},
                  result{h, 2u},
              }));
}

TEST_F(LongChain, ListIncludedFiles) {
  EXPECT_THAT(list_included_files::from_graph(graph, sources()),
              UnorderedElementsAreArray({
                  result{a, 1u},
                  result{b, 1u},
                  result{c, 1u},
                  result{d, 1u},
                  result{e, 1u},
                  result{f, 1u},
                  result{g, 1u},
                  result{h, 1u},
                  result{i, 1u},
                  result{j, 1u},
              }));
}

} // namespace
