#ifndef INCLUDE_GUARD_A229377A_F884_4D97_AD9A_0D6804EC7448
#define INCLUDE_GUARD_A229377A_F884_4D97_AD9A_0D6804EC7448

// We want to determine whether we can move all inclusions for a
// header `H` from other headers into their respective source files.
// This would be useful if there is a particularly expensive utility
// component that does not show up in the interface of components
// using it, but it is a necessary part of the implementation.  Perhaps
// it can be hidden away using something like the pimpl idiom.
//
// A good example of these headers would be `<unordered_map>` or
// `<algorithm>` where they can be particularly expensive, but are rarely
// used for vocabulary types.
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
// We could attempt to move all `#include "common.hpp"` lines from
// `foo.hpp` and `bar.hpp` and instead put them inside `foo.cpp`
// and `bar.cpp`.  This would reduce the size of `main.cpp` when
// it was compiled, with no change in the size of `foo.cpp` and
// `bar.cpp`.
//
// We could do the same with removing `#include "large.hpp"` from
// `bar.hpp` and adding the include inside `bar.cpp`.
//
// For `#include "zorb.hpp"` inside `common.hpp`, we would first need
// to recommend **adding** a source file `common.cpp` in order to move
// the include inside.  That would add additional compilation work
// since we would have an additional source.  It would only become
// faster if there were enough source dependencies on `zorb.hpp`
// compared to the size of `common.cpp`.

#include "graph.hpp"

#include <initializer_list>
#include <iosfwd>
#include <span>
#include <vector>

namespace IncludeGuardian {

struct find_expensive_headers {
  struct result {
    Graph::vertex_descriptor v; //< The header file
    cost saving; //< The saving if it was removed from all headers but added to
                 //the source
  };

  /// Return the list of header files along with the total cost if
  /// the inclusion directives were moved from the header to the source
  /// file.
  static std::vector<result>
  from_graph(const Graph &graph,
             std::span<const Graph::vertex_descriptor> sources,
             std::int64_t minimum_token_count_cut_off = 0,
             unsigned maximum_dependencies = UINT_MAX);
  static std::vector<result>
  from_graph(const Graph &graph,
             std::initializer_list<Graph::vertex_descriptor> sources,
             std::int64_t minimum_token_count_cut_off = 0,
             unsigned maximum_dependencies = UINT_MAX);
};

bool operator==(const find_expensive_headers::result &lhs,
                const find_expensive_headers::result &rhs);
bool operator!=(const find_expensive_headers::result &lhs,
                const find_expensive_headers::result &rhs);
std::ostream &operator<<(std::ostream &out,
                         const find_expensive_headers::result &v);

} // namespace IncludeGuardian

#endif