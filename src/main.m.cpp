#include "print_graph_factory.hpp"
#include "find_expensive_includes.hpp"

#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>

#include <iostream>

namespace {

enum class output {
	dot_graph,
	most_expensive,
};

} // close unnamed namespace

int main(int argc, const char** argv) {
	using namespace IncludeGuardian;

	llvm::cl::OptionCategory MyToolCategory("my-tool options");

	llvm::cl::opt<output> output("output", llvm::cl::desc("Choose the output"),
		llvm::cl::values(
			llvm::cl::OptionEnumValue(
				"dot-graph",
				static_cast<int>(output::dot_graph),
				"DOT graph"),
			llvm::cl::OptionEnumValue(
				"most-expensive",
				static_cast<int>(output::most_expensive),
				"List of most expensive include directives")));

	llvm::Expected<clang::tooling::CommonOptionsParser> optionsParser =
		clang::tooling::CommonOptionsParser::create(argc, argv, MyToolCategory);
	if (!optionsParser) {
		std::cerr << toString(optionsParser.takeError());
		return 1;
	}
	clang::tooling::ClangTool Tool(optionsParser->getCompilations(),
		optionsParser->getSourcePathList());

	switch (output) {
		case output::dot_graph: {
			print_graph_factory f;
			return Tool.run(&f);
		}
		case output::most_expensive: {
			print_graph_factory f;
			std::vector<include_directive> results;
			find_expensive_includes g(results);
			const int rc = Tool.run(&g);
			std::sort(results.begin(), results.end(), [](auto&& l, auto&& r) {
				return l.savingInBytes > r.savingInBytes;
				});
			for (const include_directive& i : results) {
				std::cout << "(" << i.savingInBytes << " bytes) " << i.file.filename().string() << " #include <" << i.include << ">\n";
			}
			return rc;
		}
	}
}