#include "graph.hpp"

#include "analysis_test_fixtures.hpp"
#include "matchers.hpp"

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/graph/adj_list_serialize.hpp>

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <istream>
#include <ostream>

using namespace IncludeGuardian;
using namespace testing;

namespace {

TEST_F(DiamondGraph, SerializeGraph) {
  std::ostringstream out;
  boost::archive::text_oarchive oa(out);
  oa << graph;
  Graph actual;
  std::istringstream in(out.str());
  boost::archive::text_iarchive ia(in);
  ia >> actual;
  EXPECT_THAT(graph, GraphsAreEquivalent(actual));
}

TEST_F(MultiLevel, SerializeGraph) {
  std::ostringstream out;
  boost::archive::text_oarchive oa(out);
  oa << graph;
  Graph actual;
  std::istringstream in(out.str());
  boost::archive::text_iarchive ia(in);
  ia >> actual;
  EXPECT_THAT(graph, GraphsAreEquivalent(actual));
}

TEST_F(LongChain, SerializeGraph) {
  std::ostringstream out;
  boost::archive::text_oarchive oa(out);
  oa << graph;
  Graph actual;
  std::istringstream in(out.str());
  boost::archive::text_iarchive ia(in);
  ia >> actual;
  EXPECT_THAT(graph, GraphsAreEquivalent(actual));
}

TEST_F(WInclude, SerializeGraph) {
  std::ostringstream out;
  boost::archive::text_oarchive oa(out);
  oa << graph;
  Graph actual;
  std::istringstream in(out.str());
  boost::archive::text_iarchive ia(in);
  ia >> actual;
  EXPECT_THAT(graph, GraphsAreEquivalent(actual));
}

TEST_F(CascadingInclude, SerializeGraph) {
  std::ostringstream out;
  boost::archive::text_oarchive oa(out);
  oa << graph;
  Graph actual;
  std::istringstream in(out.str());
  boost::archive::text_iarchive ia(in);
  ia >> actual;
  EXPECT_THAT(graph, GraphsAreEquivalent(actual));
}

TEST_F(ComplexCascadingInclude, SerializeGraph) {
  std::ostringstream out;
  boost::archive::text_oarchive oa(out);
  oa << graph;
  Graph actual;
  std::istringstream in(out.str());
  boost::archive::text_iarchive ia(in);
  ia >> actual;
  EXPECT_THAT(graph, GraphsAreEquivalent(actual));
}

} // namespace
