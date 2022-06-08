#ifndef INCLUDE_GUARD_9E4C7C9E_6B70_4EF5_B47D_6B12DDDA7316
#define INCLUDE_GUARD_9E4C7C9E_6B70_4EF5_B47D_6B12DDDA7316

#include "graph.hpp"

#include <iosfwd>

namespace IncludeGuardian {

/// This component is a concrete implementation of a `FrontEndActionFactory`
/// that will print out a DOT file representing a DAG of the include
/// directives.
struct dot_graph {
  static void print(const Graph &graph, std::ostream &stream);
};

} // namespace IncludeGuardian

#endif
