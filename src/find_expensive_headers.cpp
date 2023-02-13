#include "find_expensive_headers.hpp"

#include "dfs.hpp"
#include "get_total_cost.hpp"
#include "reachability_graph.hpp"

#include <boost/units/io.hpp>

#include <execution>
#include <iomanip>
#include <list>
#include <numeric>
#include <optional>
#include <ostream>

namespace IncludeGuardian {

namespace {

template <typename G>
get_total_cost::result
from_graph_ref(const G &graph,
               std::span<const typename G::vertex_descriptor> sources) {

  std::vector<get_total_cost::result> source_cost(num_vertices(graph));
  std::transform(
      std::execution::par, sources.begin(), sources.end(), source_cost.data(),
      [&](const typename G::vertex_descriptor source) {
        dfs_adaptor dfs(graph);
        get_total_cost::result total;
        for (const typename G::vertex_descriptor v : dfs.from(source)) {
          total.true_cost += graph[v]->true_cost();
          if (graph[v]->is_precompiled) {
            total.precompiled += graph[v]->underlying_cost;
          }
        }
        return total;
      });
  return std::reduce(source_cost.begin(), source_cost.end());
}

// Return the total file size for all vertices that are unreachable from
// `source` if no files ever included `file` + an optional extra cost that
// would occur if we needed to add a new source file.
std::optional<cost> total_file_size_of_unreachable(
    const Graph &graph, cost cost_before,
    std::span<const Graph::vertex_descriptor> sources,
    Graph::vertex_descriptor file,
    const std::int64_t minimum_token_count_cut_off) {

  // If we don't include this file ourselves then there's no need to
  // check further
  if (graph[file].internal_incoming == 0) {
    return std::nullopt;
  }

  const cost best_case_saving =
      get_total_cost::from_graph(graph, {file}).true_cost;

  // If **every** source saved the full amount and this
  // doesn't hit the target we can exit early
  if (static_cast<std::int64_t>(best_case_saving.token_count * sources.size()) <
      minimum_token_count_cut_off) {
    return std::nullopt;
  }

  // Build up a new graph that does not have any header file include `file`
  // but instead the corresponding `source` file does
  using NewGraph = boost::adjacency_list<boost::vecS, boost::vecS,
                                         boost::directedS, const file_node *>;
  NewGraph new_graph;

  // Add all vertices
  for (const Graph::vertex_descriptor v :
       boost::make_iterator_range(vertices(graph))) {
    add_vertex(&graph[v], new_graph);
  }

  // Mark all sources
  std::vector<bool> is_source(num_vertices(graph));
  for (const Graph::vertex_descriptor source : sources) {
    is_source[source] = true;
  }

  std::list<file_node> fake_file_nodes;
  std::vector<Graph::vertex_descriptor> new_sources;

  // If we have an include
  std::vector<bool> headers_including_file(num_vertices(graph));
  int including_header_count = 0;
  for (const Graph::edge_descriptor &e :
       boost::make_iterator_range(edges(graph))) {
    const Graph::vertex_descriptor s = source(e, graph);
    const Graph::vertex_descriptor t = target(e, graph);
    if (!graph[s].is_external && t == file) {
      if (!is_source[s]) {
        // If we're a header and we include `file`, then don't add
        // the edge, but instead add an edge between our source
        // file and `file` (or create a source if we don't have one)
        headers_including_file[s] = true;
        ++including_header_count;
        if (graph[s].component.has_value()) {
          const Graph::vertex_descriptor source = *graph[s].component;
          if (is_source[source]) {
            // This should always be true, but maybe we made a mistake
            // in our "is source" heuristic when building
            add_edge(source, t, new_graph);
          } else {
            assert(false);
          }
        } else {
          // If the header didn't have a source, we'd need to create a
          // new one
          const Graph::vertex_descriptor new_source =
              add_vertex(&fake_file_nodes.emplace_back(), new_graph);
          new_sources.push_back(new_source);
          add_edge(new_source, t, new_graph);
          add_edge(new_source, s, new_graph);
        }
        continue;
      }
    }

    add_edge(s, t, new_graph);
  }

  std::vector<Graph::vertex_descriptor> full_sources;
  if (!new_sources.empty()) {
    full_sources.resize(sources.size() + new_sources.size());
    const auto it =
        std::copy(sources.begin(), sources.end(), full_sources.begin());
    std::copy(new_sources.begin(), new_sources.end(), it);
    sources = full_sources;
  }

  const cost cost_after = from_graph_ref(new_graph, sources).true_cost;
  return cost_before - cost_after;
}

int count_headers(const Graph &graph, const Graph::vertex_descriptor v,
                  const std::vector<bool> &is_source) {
  const auto [begin, end] = in_edges(v, graph);
  return std::accumulate(
      begin, end, 0, [&](int acc, const Graph::edge_descriptor &e) {
        const Graph::vertex_descriptor s = source(e, graph);
        return acc + (!graph[s].is_external && !is_source[s]);
      });
}

} // namespace

std::vector<find_expensive_headers::result> find_expensive_headers::from_graph(
    const Graph &graph, std::span<const Graph::vertex_descriptor> sources,
    const std::int64_t minimum_token_count_cut_off,
    const unsigned maximum_dependencies) {
  std::mutex m;
  std::vector<find_expensive_headers::result> results;
  if (sources.empty()) {
    return results;
  }

  reachability_graph reach(graph);
  std::vector<bool> is_source(num_vertices(graph));
  for (const Graph::vertex_descriptor source : sources) {
    is_source[source] = true;
  }

  const cost cost_before = get_total_cost::from_graph(graph, sources).true_cost;
  const auto [begin, end] = vertices(graph);
  std::for_each(
      std::execution::par, begin, end,
      [&](const Graph::vertex_descriptor file) {
        // If we do not include this file ourselves, there is no point
        // performing the analysis
        if (graph[file].internal_incoming == 0) {
          return;
        }

        const std::optional<cost> saving = total_file_size_of_unreachable(
            graph, cost_before, sources, file, minimum_token_count_cut_off);

        if (saving.has_value() &&
            saving->token_count >= minimum_token_count_cut_off) {
          // There are ways to avoid this mutex, but if the
          // `minimum_size_cut_off` is large enough, it's relatively
          // rare to enter this if statement
          std::lock_guard g(m);
          results.emplace_back(file, *saving,
                               count_headers(graph, file, is_source));
        }
      });
  return results;
}

std::vector<find_expensive_headers::result> find_expensive_headers::from_graph(
    const Graph &graph, std::initializer_list<Graph::vertex_descriptor> sources,
    const std::int64_t minimum_token_count_cut_off,
    const unsigned maximum_dependencies) {
  return from_graph(graph, std::span(sources.begin(), sources.end()),
                    minimum_token_count_cut_off, maximum_dependencies);
}

bool operator==(const find_expensive_headers::result &lhs,
                const find_expensive_headers::result &rhs) {
  return lhs.v == rhs.v && lhs.saving == rhs.saving &&
         lhs.header_reference_count == rhs.header_reference_count;
}

bool operator!=(const find_expensive_headers::result &lhs,
                const find_expensive_headers::result &rhs) {
  return !(lhs == rhs);
}

std::ostream &operator<<(std::ostream &out,
                         const find_expensive_headers::result &v) {
  return out << '[' << v.v << " saving=" << v.saving
             << " hdr count=" << v.header_reference_count << ']';
}

} // namespace IncludeGuardian
