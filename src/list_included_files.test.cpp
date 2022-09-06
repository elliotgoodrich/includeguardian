#include "list_included_files.hpp"

#include "analysis_test_fixtures.hpp"

#include <gtest/gtest.h>

#include <utility>

using namespace IncludeGuardian;

namespace {

bool test_sort(const list_included_files::result &lhs,
               const list_included_files::result &rhs) {
  return lhs.v < rhs.v;
}

TEST_F(DiamondGraph, ListIncludedFiles) {
  std::vector<list_included_files::result> actual =
      list_included_files::from_graph(graph, sources());
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<list_included_files::result> expected = {
      {a, 1u},
      {b, 1u},
      {c, 1u},
      {d, 1u},
  };
  EXPECT_EQ(actual, expected);
}

TEST_F(MultiLevel, ListIncludedFiles) {
  std::vector<list_included_files::result> actual =
      list_included_files::from_graph(graph, sources());
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<list_included_files::result> expected = {
      {a, 1u},
      {b, 1u},
      {c, 1u},
      {d, 2u},
      {e, 1u},
      {f, 2u},
      {g, 1u},
      {h, 2u},
  };
  EXPECT_EQ(actual, expected);
}

TEST_F(LongChain, ListIncludedFiles) {
  std::vector<list_included_files::result> actual =
      list_included_files::from_graph(graph, sources());
  std::sort(actual.begin(), actual.end(), test_sort);
  const std::vector<list_included_files::result> expected = {
      {a, 1u},
      {b, 1u},
      {c, 1u},
      {d, 1u},
      {e, 1u},
      {f, 1u},
      {g, 1u},
      {h, 1u},
      {i, 1u},
      {j, 1u},
  };
}

} // namespace
