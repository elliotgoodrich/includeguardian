#include "build_graph.hpp"
#include "dot_graph.hpp"
#include "find_expensive_files.hpp"
#include "find_expensive_headers.hpp"
#include "find_expensive_includes.hpp"
#include "find_unnecessary_sources.hpp"
#include "get_total_cost.hpp"
#include "graph.hpp"
#include "list_included_files.hpp"

#include <boost/units/io.hpp>

#include <llvm/Support/CommandLine.h.>
#include <llvm/Support/VirtualFileSystem.h.>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>

using namespace IncludeGuardian;

namespace {

const std::pair<std::string_view, build_graph::file_type> lookup[] = {
    {"cpp", build_graph::file_type::source},
    {"c", build_graph::file_type::source},
    {"cc", build_graph::file_type::source},
    {"C", build_graph::file_type::source},
    {"cxx", build_graph::file_type::source},
    {"c++", build_graph::file_type::source},
    {"hpp", build_graph::file_type::header},
    {"h", build_graph::file_type::header},
    {"hh", build_graph::file_type::header},
    {"H", build_graph::file_type::header},
    {"hxx", build_graph::file_type::header},
    {"h++", build_graph::file_type::header},
    {"", build_graph::file_type::ignore},
};
build_graph::file_type map_ext(std::string_view file) {
  const auto dot = std::find(file.rbegin(), file.rend(), '.').base();
  const std::string_view ext(dot, file.end());
  // Use end-1 because if we fail to find then the true last element is 'ignore'
  return std::find_if(std::begin(lookup), std::end(lookup) - 1,
                      [=](auto p) { return p.first == ext; })
      ->second;
}

class stopwatch {
  std::chrono::steady_clock::time_point m_start;

public:
  stopwatch() : m_start(std::chrono::steady_clock::now()) {}

  std::chrono::steady_clock::duration restart() {
    auto const now = std::chrono::steady_clock::now();
    return now - std::exchange(m_start, now);
  }
};

enum class output {
  dot_graph,
  list_files,
  most_expensive,
};

} // namespace

int main(int argc, const char **argv) {
  // Use the user's locale to format numbers etc.
  std::cout.imbue(std::locale(""));

  llvm::cl::OptionCategory MyToolCategory("my-tool options");

  llvm::cl::opt<output> output(
      "output", llvm::cl::desc("Choose the output"),
      llvm::cl::values(
          llvm::cl::OptionEnumValue(
              "dot-graph", static_cast<int>(output::dot_graph), "DOT graph"),
          llvm::cl::OptionEnumValue(
              "list-files", static_cast<int>(output::list_files), "List files"),
          llvm::cl::OptionEnumValue(
              "most-expensive", static_cast<int>(output::most_expensive),
              "List of most expensive include directives")));

  llvm::cl::opt<std::string> dir("dir", llvm::cl::desc("Choose the directory"));

  llvm::cl::list<std::string> include_dirs(
      "i", llvm::cl::desc("Additional include directories"),
      llvm::cl::ZeroOrMore);
  if (!llvm::cl::ParseCommandLineOptions(argc, argv)) {
    return 1;
  }

  stopwatch timer;

  std::vector<std::filesystem::path> include_dir_paths(include_dirs.size());
  std::transform(include_dirs.begin(), include_dirs.end(),
                 include_dir_paths.begin(),
                 [](const std::string &s) { return std::filesystem::path(s); });

  llvm::Expected<build_graph::result> result =
      build_graph::from_dir(dir.getValue(), include_dir_paths,
                            llvm::vfs::getRealFileSystem(), map_ext);

  if (!result) {
    // TODO: error message
    std::cerr << "Error";
    return 1;
  }

  std::cout << "Graph built in "
            << duration_cast<std::chrono::milliseconds>(timer.restart())
            << "\n";

  const auto &graph = result->graph;
  const auto &sources = result->sources;
  const auto &missing = result->missing_includes;
  std::cout << "Found " << num_vertices(graph) << " files and "
            << num_edges(graph) << " include directives.\n";
  if (!missing.empty()) {
    std::cout << "There are " << missing.size() << " missing files\n  ";
    std::copy(missing.begin(), missing.end(),
              std::ostream_iterator<std::string>(std::cout, "  \n  "));
  } else {
    std::cout << "All includes found :)";
  }
  std::cout << "\n";

  switch (output) {
  case output::dot_graph: {
    dot_graph::print(graph, std::cout);
    std::cout << "Graph printed in "
              << duration_cast<std::chrono::milliseconds>(timer.restart())
              << "\n";
    return 0;
  }
  case output::list_files: {
    const cost total_project_cost = get_total_cost::from_graph(graph, sources);
    std::cout << "Total file size found " << std::setprecision(2) << std::fixed
              << boost::units::binary_prefix << total_project_cost.file_size
              << " and " << total_project_cost.token_count
              << " total preprocessing tokens found in "
              << duration_cast<std::chrono::milliseconds>(timer.restart())
              << "\n";
    const double percent_cut_off = 0.005;
    {
      std::vector<list_included_files::result> results =
          list_included_files::from_graph(graph, sources);
      std::cout << "Files found in "
                << duration_cast<std::chrono::milliseconds>(timer.restart())
                << "\n";
      std::sort(results.begin(), results.end(),
                [&](const list_included_files::result &l,
                    const list_included_files::result &r) {
                  return l.source_that_can_reach_it_count *
                             graph[l.v].cost.token_count >
                         r.source_that_can_reach_it_count *
                             graph[r.v].cost.token_count;
                });
      for (const list_included_files::result &i : results) {
        const cost c = i.source_that_can_reach_it_count * graph[i.v].cost;
        const double percentage =
            (100.0 * c.token_count) / total_project_cost.token_count;
        std::cout << std::setprecision(2) << std::fixed << c.token_count << " ("
                  << percentage << "%) " << graph[i.v].path.filename().string()
                  << " x" << i.source_that_can_reach_it_count << "\n";
      }
    }
    return 0;
  }
  case output::most_expensive: {
    const cost total_project_cost = get_total_cost::from_graph(graph, sources);
    std::cout << "Total file size found " << std::setprecision(2) << std::fixed
              << boost::units::binary_prefix << total_project_cost.file_size
              << " and " << total_project_cost.token_count
              << " total preprocessing tokens found in "
              << duration_cast<std::chrono::milliseconds>(timer.restart())
              << "\n";
    const double percent_cut_off = 0.005;
    {
      std::vector<include_directive_and_cost> results =
          find_expensive_includes::from_graph(
              graph, sources, total_project_cost.token_count * percent_cut_off);
      std::cout << "Includes analyzed in "
                << duration_cast<std::chrono::milliseconds>(timer.restart())
                << "\n";
      std::sort(results.begin(), results.end(),
                [](const include_directive_and_cost &l,
                   const include_directive_and_cost &r) {
                  return l.saving.token_count > r.saving.token_count;
                });
      for (const include_directive_and_cost &i : results) {
        const double percentage =
            (100.0 * i.saving.token_count) / total_project_cost.token_count;
        std::cout << std::setprecision(2) << std::fixed << i.saving.token_count
                  << " (" << percentage << "%) from "
                  << i.file.filename().string() << "L#" << i.include->lineNumber
                  << " remove #include " << i.include->code << "\n";
      }
    }

    {
      std::vector<find_expensive_headers::result> results =
          find_expensive_headers::from_graph(
              graph, sources, total_project_cost.token_count * percent_cut_off);
      std::cout << "\nHeaders analyzed in "
                << duration_cast<std::chrono::milliseconds>(timer.restart())
                << "\n";
      std::sort(results.begin(), results.end(),
                [](const find_expensive_headers::result &l,
                   const find_expensive_headers::result &r) {
                  return l.saving.token_count > r.saving.token_count;
                });
      for (const find_expensive_headers::result &i : results) {
        const double percentage =
            (100.0 * i.saving.token_count) / total_project_cost.token_count;
        std::cout << std::setprecision(2) << std::fixed << i.saving.token_count
                  << " (" << percentage << "%) removing " << graph[i.v].incoming
                  << " references to " << graph[i.v].path << "\n";
      }
    }

    {
      // Assume that each "expensive" file could be reduced this much
      const double assumed_reduction = 0.50;
      std::vector<file_and_cost> results = find_expensive_files::from_graph(
          graph, sources,
          total_project_cost.token_count * percent_cut_off / assumed_reduction);
      std::cout << "\nFiles analyzed in "
                << duration_cast<std::chrono::milliseconds>(timer.restart())
                << "\n";
      std::sort(results.begin(), results.end(),
                [](const file_and_cost &l, const file_and_cost &r) {
                  return static_cast<std::size_t>(l.node->cost.token_count) *
                             l.sources >
                         static_cast<std::size_t>(r.node->cost.token_count) *
                             r.sources;
                });
      for (const file_and_cost &i : results) {
        const unsigned saving =
            i.sources * assumed_reduction * i.node->cost.token_count;
        const double percentage =
            (100.0 * saving) / total_project_cost.token_count;
        std::cout << std::setprecision(2) << std::fixed << saving << " ("
                  << percentage << "%) from "
                  << std::filesystem::path(i.node->path).filename().string()
                  << " by simplifing or splitting by "
                  << 100 * assumed_reduction << "%\n";
      }
    }

    {
      std::vector<find_unnecessary_sources::result> results =
          find_unnecessary_sources::from_graph(
              graph, sources,
              INT_MIN); // total_project_cost.token_count * percent_cut_off);
      std::cout << "\nSources analyzed in "
                << duration_cast<std::chrono::milliseconds>(timer.restart())
                << "\n";
      std::sort(results.begin(), results.end(),
                [](const find_unnecessary_sources::result &l,
                   const find_unnecessary_sources::result &r) {
                  return l.total_saving().token_count >
                         r.total_saving().token_count;
                });
      for (const find_unnecessary_sources::result &i : results) {
        const double percentage = (100.0 * i.total_saving().token_count) /
                                  total_project_cost.token_count;
        std::cout << std::setprecision(2) << std::fixed
                  << i.total_saving().token_count << " (" << percentage
                  << "%) deleting " << graph[i.source].path
                  << " and putting its contents in "
                  << graph[*graph[i.source].component].path << "\n";
      }
    }
    return 0;
  }
  }
}