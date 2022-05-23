#include "build_graph.hpp"

#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

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
  std::vector<clang::tooling::CompileCommand>
  getCompileCommands(clang::StringRef FilePath) const final {
    if (FilePath == "main.cpp") {
      return {{"/tests/",
               "main.cpp",
               {"/usr/bin/clang++", "-o", "out.o", "main.cpp"},
               "out.o"}};
    } else {
      return {};
    }
  }

  /// Returns the list of all files available in the compilation database.
  ///
  /// By default, returns nothing. Implementations should override this if they
  /// can enumerate their source files.
  std::vector<std::string> getAllFiles() const final { return {"main.cpp"}; }
};

using namespace IncludeGuardian;
TEST(BuildGraphTest, Snapshot) {

  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  fs->addFile("/tests/header.hpp", 0,
              llvm::MemoryBuffer::getMemBuffer("// hi"));
  fs->addFile("/tests/main.cpp", 0,
              llvm::MemoryBuffer::getMemBuffer("#include \"header.hpp\"\n"
              "#pragma override_file_size(1024)\n"));
  TestCompilationDatabase db;
  std::string sources[] = {"main.cpp"};
  llvm::Expected<std::pair<Graph, std::vector<Graph::vertex_descriptor>>>
      results = build_graph::from_compilation_db(db, sources, fs);
  ASSERT_TRUE(static_cast<bool>(results));

  const auto& [graph, source_descriptors] = *results;
  EXPECT_EQ(num_vertices(graph), 2);
  EXPECT_EQ(num_edges(graph), 1);
}

} // namespace
