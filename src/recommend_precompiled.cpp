#include "recommend_precompiled.hpp"

#include "reachability_graph.hpp"

#include <boost/units/io.hpp>

#include <execution>
#include <iomanip>
#include <numeric>
#include <ostream>

namespace IncludeGuardian {

namespace {

enum search_state : std::uint8_t {
  not_seen,
  seen,
};

} // namespace

std::vector<recommend_precompiled::result> recommend_precompiled::from_graph(
    const Graph &graph, std::span<const Graph::vertex_descriptor> sources,
    const int minimum_token_count_cut_off, const double minimum_saving_ratio) {
  assert(minimum_saving_ratio > 0.0);
  const reachability_graph reach(graph);
  std::mutex m;
  std::vector<recommend_precompiled::result> results;
  const auto [begin, end] = vertices(graph);
  std::for_each(begin, end, [&](const Graph::vertex_descriptor file) {
    const file_node &f = graph[file];

    // For now, we should avoid recommending files that we have not explicitly
    // included ourselves because this may recommend private headers in
    // external libraries that may be removed in further updates.
    if (f.internal_incoming == 0) {
      return;
    }

    // Little point adding external files to precompiled headers as it
    // pessimises rebuild
    if (!f.is_external) {
      return;
    }

    // No benefit for checking a file that's already precompiled
    if (f.is_precompiled) {
      return;
    }

    recommend_precompiled::result r;
    r.v = file;

    // DFS from `file` and mark all descendants that were not previously
    // precompiled
    std::vector<bool> newly_precompiled(num_vertices(graph));
    int newly_precompiled_count = 0;

    std::vector<std::uint8_t> state(num_vertices(graph), not_seen);
    std::vector<Graph::vertex_descriptor> stack;
    stack.push_back(file);
    while (!stack.empty()) {
      const Graph::vertex_descriptor v = stack.back();
      stack.pop_back();
      if (state[v] == seen) {
        continue;
      }

      // If we're already precompiled then all our descendents are
      if (graph[v].is_precompiled) {
        continue;
      }

      newly_precompiled[v] = true;
      ++newly_precompiled_count;

      r.extra_precompiled_size += graph[v].underlying_cost;
      state[v] = seen;
      const auto [begin, end] = adjacent_vertices(v, graph);
      stack.insert(stack.end(), begin, end);
    }

    // Not only do we need to beat the `minimum_token_count_cut_off`, but
    // as we know the extra size of the precompiled files, we have to
    // beat that by the specified ratio.
    const auto cutoff_token_count = std::max<int>(
            minimum_saving_ratio * r.extra_precompiled_size.token_count,
        minimum_token_count_cut_off
    );

    // Go through all sources that included `file` and find what dependencies
    // of `file` are reachable through other means.
    for (std::size_t i = 0; i < sources.size(); ++i) {
      // If the file is so small that we couldn't possibly exceeed
      // our threshold with the remaining files, then give up
      const int remaining_sources = sources.size() - i;
      if (r.extra_precompiled_size.token_count * remaining_sources +
              r.saving.token_count <
          cutoff_token_count ) {
        return;
      }

      // DFS from our source and sum up all files we traverse that
      // are newly precompiled and sum up their size
      stack.push_back(sources[i]);
      while (!stack.empty()) {
        const Graph::vertex_descriptor v = stack.back();
        stack.pop_back();
        if (state[v] == seen) {
          continue;
        }

        // If we found a file that is now added to the precompiled list
        // sum up its cost
        if (newly_precompiled[v]) {
          r.saving += graph[v].underlying_cost;
        }

        state[v] = seen;
        const auto [begin, end] = adjacent_vertices(v, graph);
        stack.insert(stack.end(), begin, end);
      }

      // Reset the state before we can use it again
      state.assign(state.size(), not_seen);
    }

    // TODO: Subtract the cost of the precompiled header

    // 
    if (r.saving.token_count >= cutoff_token_count) {
      // There are ways to avoid this mutex, but if the
      // `minimum_size_cut_off` is large enough, it's relatively
      // rare to enter this if statement
      std::lock_guard g(m);
      results.emplace_back(r);
    }
  });
  return results;
}

std::vector<recommend_precompiled::result> recommend_precompiled::from_graph(
    const Graph &graph, std::initializer_list<Graph::vertex_descriptor> sources,
    const int minimum_token_count_cut_off, const double minimum_saving_ratio) {
  return from_graph(graph, std::span(sources.begin(), sources.end()),
                    minimum_token_count_cut_off, minimum_saving_ratio);
}

bool operator==(const recommend_precompiled::result &lhs,
                const recommend_precompiled::result &rhs) {
  return lhs.v == rhs.v && lhs.saving == rhs.saving &&
         lhs.extra_precompiled_size == rhs.extra_precompiled_size;
}

bool operator!=(const recommend_precompiled::result &lhs,
                const recommend_precompiled::result &rhs) {
  return !(lhs == rhs);
}

std::ostream &operator<<(std::ostream &out,
                         const recommend_precompiled::result &v) {
  return out << '[' << v.v << " saving=" << v.saving
             << "padded_precompiled=" << v.extra_precompiled_size << ']';
}

} // namespace IncludeGuardian
