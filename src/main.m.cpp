#include "build_graph.hpp"
#include "dot_graph.hpp"
#include "find_expensive_includes.hpp"
#include "get_total_cost.hpp"
#include "graph.hpp"

#include <clang/Tooling/CommonOptionsParser.h>

#include <chrono>
#include <iostream>
#include <string>
#include <utility>

namespace {

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
  most_expensive,
};

} // namespace

int main(int argc, const char **argv) {
  using namespace IncludeGuardian;

  llvm::cl::OptionCategory MyToolCategory("my-tool options");

  llvm::cl::opt<output> output(
      "output", llvm::cl::desc("Choose the output"),
      llvm::cl::values(
          llvm::cl::OptionEnumValue(
              "dot-graph", static_cast<int>(output::dot_graph), "DOT graph"),
          llvm::cl::OptionEnumValue(
              "most-expensive", static_cast<int>(output::most_expensive),
              "List of most expensive include directives")));

  llvm::Expected<clang::tooling::CommonOptionsParser> optionsParser =
      clang::tooling::CommonOptionsParser::create(argc, argv, MyToolCategory);
  if (!optionsParser) {
    std::cerr << toString(optionsParser.takeError());
    return 1;
  }

  stopwatch timer;
  llvm::Expected<std::pair<Graph, std::vector<Graph::vertex_descriptor>>>
      result =
          build_graph::from_compilation_db(optionsParser->getCompilations(),
                                           optionsParser->getSourcePathList(),
                                           llvm::vfs::getRealFileSystem());
  if (!result) {
    // TODO: error message
    std::cerr << "Error";
    return 1;
  }

  std::cout << "Graph built in " << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";

  auto &[graph, sources] = *result;

  switch (output) {
  case output::dot_graph: {
    dot_graph::print(graph, std::cout);
    std::cout << "Graph printed in " << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";
    return 0;
  }
  case output::most_expensive: {
    const std::size_t total_project_cost =
        get_total_cost::from_graph(graph, sources);
    std::cout << "Total size found (" << total_project_cost << " bytes) in " << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";
    const double percent_cut_off = 0.005;
    std::vector<include_directive_and_cost> results =
        find_expensive_includes::from_graph(
            graph, sources, total_project_cost * percent_cut_off);
    std::cout << "Includes found in " << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";
    std::sort(results.begin(), results.end(),
              [](const include_directive_and_cost &l,
                 const include_directive_and_cost &r) {
                return l.savingInBytes > r.savingInBytes;
              });
    for (const include_directive_and_cost &i : results) {
      const double percentage = (100.0 * i.savingInBytes) / total_project_cost;
      std::cout << i.savingInBytes << " bytes (" << percentage << "%) from "
                << i.file.filename().string() << " remove #include "
                << i.include << "\n";
    }
    return 0;
  }
  }
}