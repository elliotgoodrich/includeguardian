#ifndef INCLUDE_GUARD_98951852_378C_4F08_8EBD_7D4D7CEC15C9
#define INCLUDE_GUARD_98951852_378C_4F08_8EBD_7D4D7CEC15C9

// We want to determine the best additions to a precompiled header
// file for a given set of sources.
//
// This should list files that are commonly included by a large
// number of sources and/or are particularly large themselves.  If
// files already have a significant overlap with the existing
// contents of the precompiled header, the reported savings will
// reflect this.
//
// For example, given the set of files below:
//
//   +-----------------------------------+
//   | foo.cpp     main.cpp      bar.cpp |
//   |      \     /        \    /        |
//   |       \   /          \  /         |
//   |      foo.hpp       bar.hpp        |
//   |         \            /  \         |
//   |          \          /    \        |
//   |           common.hpp    large.hpp |
//   |               |                   |
//   |               |                   |
//   |            zorb.hpp               |
//   +-----------------------------------+
//
// It would most likely be recommended to add `zorb.hpp`,
// `common.hpp`, and `large.hpp` to the precompiled header since
// they are included by multiple sources.
//
// If `zorb.hpp` was already part of the precompiled header, then
// `large.hpp` could be recommended as the next best candidate as
// it is (assumedly) a large file, despite only being included by
// 2 sources instead of `common.hpp`s 3.

#include "graph.hpp"

#include <initializer_list>
#include <iosfwd>
#include <span>
#include <vector>

namespace IncludeGuardian {

struct recommend_precompiled {
  struct result {
    Graph::vertex_descriptor v; //< The header file
    cost saving; //< The saving if it was removed from all headers
    cost extra_precompiled_size; //< The additional size added to the current precompiled header
  };

  /// Return the list of files that if added to a precompiled header, would
  /// give a saving of at least `minimum_token_count_cut_off` and they save
  /// a multiple of more than `minimum_saving_ratio` preprocessor tokens
  /// compared to the tokens added to the precompiled header.
  static std::vector<result>
  from_graph(const Graph &graph,
             std::span<const Graph::vertex_descriptor> sources,
             int minimum_token_count_cut_off = 0,
             double minimum_saving_ratio = 1.5);
  static std::vector<result>
  from_graph(const Graph &graph,
             std::initializer_list<Graph::vertex_descriptor> sources,
             int minimum_token_count_cut_off = 0,
             double minimum_saving_ratio = 1.5);
};

bool operator==(const recommend_precompiled::result &lhs,
                const recommend_precompiled::result &rhs);
bool operator!=(const recommend_precompiled::result &lhs,
                const recommend_precompiled::result &rhs);
std::ostream &operator<<(std::ostream &out,
                         const recommend_precompiled::result &v);

} // namespace IncludeGuardian

#endif