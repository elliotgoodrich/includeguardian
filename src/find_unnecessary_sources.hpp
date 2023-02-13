#ifndef INCLUDE_GUARD_E38F92F5_F9B0_4D49_BD44_427098F84AC3
#define INCLUDE_GUARD_E38F92F5_F9B0_4D49_BD44_427098F84AC3

// We want to determine what source files could be removed and have
// all of their contents and includes into their respective header
// file.  This occurs if the size of the source file and includes
// are comparatively small compared to its header file and the header
// file is not included very often.
//
// A good example would be a "god-object" class that contains a lot
// of objects but almost no implementation.  It may be better to
// inline all of the accessors and get rid of the source file.
//
// For example, given the set of files below:
//
//   +--------------------------------+
//   |   .------- main.cpp -------.   |
//   |  /                \         \  |
//   | | foo.cpp  bar.cpp | zed.cpp | |
//   | |    |        |    |    |   /  |
//   |  \   |        |   /     |  /   |
//   |   foo.hpp     |  / zed.hpp     |
//   |          \    | / /            |
//   |            bar.hpp             |
//   +--------------------------------+
//
// We could attempt to remove `foo.cpp`, `bar.cpp`, or `zed.cpp`
// and move the contents into their header files.  In this example
// the source files only include their header, but if they had
// additional includes then those would also be moved to the header.
//
// Removing `foo.cpp` would add the cost of `foo.cpp` when compiling
// `main.cpp`, but we would lose the cost of compiling the source
// file `foo.cpp` that included `foo.hpp` and `bar.hpp`.

#include "graph.hpp"

#include <initializer_list>
#include <iosfwd>
#include <span>
#include <vector>

namespace IncludeGuardian {

/// This component will output the saving if we removed a source file and
/// instead put all its contents + includes into the corresponding header
struct find_unnecessary_sources {
  struct result {
    Graph::vertex_descriptor source; //< The source file
    cost saving;                     //< The saving from removing the source
    cost extra_cost; //< The extra cost from all sources including the larger
                     //header

    cost total_saving() const { return saving - extra_cost; }
  };

  /// Return the list of sources which, if removed and the contents put,
  /// inside the corresponding header, would cause a total reduction in
  /// post-processing file size of `minimum_token_count_cut_off`.
  static std::vector<result>
  from_graph(const Graph &graph,
             std::span<const Graph::vertex_descriptor> sources,
             int minimum_token_count_cut_off = 0);
  static std::vector<result>
  from_graph(const Graph &graph,
             std::initializer_list<Graph::vertex_descriptor> sources,
             int minimum_token_count_cut_off = 0);
};

bool operator==(const find_unnecessary_sources::result &lhs,
                const find_unnecessary_sources::result &rhs);
bool operator!=(const find_unnecessary_sources::result &lhs,
                const find_unnecessary_sources::result &rhs);
std::ostream &operator<<(std::ostream &out,
                         const find_unnecessary_sources::result &v);

} // namespace IncludeGuardian

#endif