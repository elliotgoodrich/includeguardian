#include "print_graph_factory.hpp"

#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>

#include <llvm/Support/VirtualFileSystem.h>

#include <gtest/gtest.h>

#include <ostream>

namespace {

class TestCompilationDatabase : public clang::tooling::CompilationDatabase {
public:
	TestCompilationDatabase() = default;

    /// Returns all compile commands in which the specified file was
    /// compiled.
    ///
    /// This includes compile commands that span multiple source files.
    /// For example, consider a project with the following compilations:
    /// $ clang++ -o test a.cc b.cc t.cc
    /// $ clang++ -o production a.cc b.cc -DPRODUCTION
    /// A compilation database representing the project would return both command
    /// lines for a.cc and b.cc and only the first command line for t.cc.
	std::vector<clang::tooling::CompileCommand> getCompileCommands(
		clang::StringRef FilePath) const final
	{
		if (FilePath == "main.cpp") {
			return { {"/tests/", "main.cpp", {"/usr/bin/clang++", "-o", "out.o", "main.cpp"}, "out.o"}};
		}
		else {
			return {};
		}
    }

     /// Returns the list of all files available in the compilation database.
     ///
     /// By default, returns nothing. Implementations should override this if they
     /// can enumerate their source files.
	std::vector<std::string> getAllFiles() const final
	{
		return { "main.cpp" };
	}
};

using namespace IncludeGuardian;
TEST(DOT_Graph, Snapshot) {

	auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
	fs->addFile("/tests/header.hpp", 0, llvm::MemoryBuffer::getMemBuffer("// hi"));
	fs->addFile("/tests/main.cpp", 0, llvm::MemoryBuffer::getMemBuffer("#include \"header.hpp\""));
	std::string errorMessage;
	TestCompilationDatabase db;
	clang::tooling::ClangTool Tool(
		db,
		{ "main.cpp" },
		std::make_shared<clang::PCHContainerOperations>(),
		fs);

	std::ostringstream stream;
	print_graph_factory f(stream);
	EXPECT_EQ(Tool.run(&f), 0);
	EXPECT_EQ(stream.str(), "digraph G {\n"
		"0[label=\"header.hpp\"][style=\"filled\"][fontcolor=\"#ffffff\"][fillcolor=\"#009900\"][fontsize=\"11pt\"];\n"
		"1[label=\"main.cpp\"][style=\"filled\"][fontcolor=\"#ffffff\"][fillcolor=\"#009900\"][fontsize=\"16pt\"];\n"
		"1->0 ;\n"
		"}\n");
}

} // close unnamed namespace
