#include "print_graph_factory.hpp"

#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>

#include <iostream>

int main(int argc, const char** argv) {
	using namespace IncludeGuardian;

	llvm::cl::OptionCategory MyToolCategory("my-tool options");

	llvm::Expected<clang::tooling::CommonOptionsParser> optionsParser =
		clang::tooling::CommonOptionsParser::create(argc, argv, MyToolCategory);
	if (!optionsParser) {
		std::cerr << toString(optionsParser.takeError());
		return 1;
	}
	clang::tooling::ClangTool Tool(optionsParser->getCompilations(),
		optionsParser->getSourcePathList());

	print_graph_factory f;
	return Tool.run(&f);
}