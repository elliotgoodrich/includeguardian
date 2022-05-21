#include "build_graph.hpp"
#include "dot_graph.hpp"
#include "find_expensive_includes.hpp"

#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

#include <iostream>
#include <string>

namespace {

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

  auto &[graph, sources] = *result;

  switch (output) {
  case output::dot_graph: {
    dot_graph::print(graph, std::cout);
    return 0;
  }
  case output::most_expensive: {
    std::vector<include_directive_and_cost> results =
        find_expensive_includes::from_graph(graph, sources);
    std::sort(results.begin(), results.end(),
              [](const include_directive_and_cost &l,
                 const include_directive_and_cost &r) {
                return l.savingInBytes > r.savingInBytes;
              });
    for (const include_directive_and_cost &i : results) {
      std::cout << "(" << i.savingInBytes << " bytes) "
                << i.file.filename().string() << " #include <" << i.include
                << ">\n";
    }
    return 0;
  }
  }
}