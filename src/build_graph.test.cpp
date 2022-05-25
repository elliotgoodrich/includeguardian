#include "build_graph.hpp"

#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/Support/VirtualFileSystem.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <ostream>

using namespace IncludeGuardian;

namespace {

// Needs to be fixed on non-window's systems
static const std::filesystem::path root = "C:";

class TestCompilationDatabase : public clang::tooling::CompilationDatabase {
  std::filesystem::path m_working_directory;

public:
  explicit TestCompilationDatabase(
      const std::filesystem::path &working_directory)
      : m_working_directory(working_directory) {}

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
      return {{m_working_directory.string(),
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

// Create an in-memory file system that would create the specified `graph`.
llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem>
make_file_system(const Graph &graph,
                 const std::filesystem::path &working_directory) {
  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  for (const Graph::vertex_descriptor &v :
       boost::make_iterator_range(vertices(graph))) {
    std::string file_contents;
    const file_node &file = graph[v];
    for (const Graph::edge_descriptor &edge :
         boost::make_iterator_range(out_edges(v, graph))) {
      file_contents += "#include ";
      file_contents += graph[edge].code;
      file_contents += '\n';
    }
    file_contents += "#pragma override_file_size(";
    file_contents += std::to_string(file.fileSizeInBytes);
    file_contents += ")\n";
    const std::filesystem::path p = working_directory / file.path;
    fs->addFile(p.string(), 0,
                llvm::MemoryBuffer::getMemBufferCopy(file_contents));
  }
  return fs;
}

void check_equal(const Graph &lhs, const Graph &rhs) {
  ASSERT_EQ(num_vertices(lhs), num_vertices(rhs));

  // We need to use an `unordered_map` as we may build up our graph in the wrong
  // order that we encounter files during our C++ preprocessor step.
  std::unordered_map<std::string_view, Graph::vertex_descriptor> file_lookup(
      num_vertices(lhs));

  for (const Graph::vertex_descriptor v :
       boost::make_iterator_range(vertices(lhs))) {
    const bool is_new = file_lookup.emplace(lhs[v].path, v).second;
    EXPECT_TRUE(is_new);
  }

  for (const Graph::vertex_descriptor v :
       boost::make_iterator_range(vertices(rhs))) {
    const auto it = file_lookup.find(rhs[v].path);
    ASSERT_NE(it, file_lookup.end());

    const auto [lhs_edge_start, lhs_edge_end] = out_edges(it->second, lhs);
    const auto [rhs_edge_start, rhs_edge_end] = out_edges(v, rhs);
    // Use `std::equal` as an easy way to iterate over 2 iterator ranges at once
    EXPECT_TRUE(std::equal(
        lhs_edge_start, lhs_edge_end, rhs_edge_start, rhs_edge_end,
        [&](Graph::edge_descriptor e, Graph::edge_descriptor f) {
          EXPECT_EQ(lhs[e].code, rhs[f].code);
          EXPECT_EQ(lhs[target(e, lhs)].path, rhs[target(f, rhs)].path);
          return true;
        }));
  }
}

TEST(BuildGraphTest, SimpleGraph) {
  Graph g;
  add_vertex({"main.cpp", 100}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  TestCompilationDatabase db(working_directory);
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  std::string sources[] = {"main.cpp"};
  llvm::Expected<std::pair<Graph, std::vector<Graph::vertex_descriptor>>>
      results = build_graph::from_compilation_db(db, sources, fs);
  check_equal(g, results->first);
}

TEST(BuildGraphTest, MultipleChildren) {
  Graph g;
  const Graph::vertex_descriptor main_cpp = add_vertex({"main.cpp", 100}, g);
  const Graph::vertex_descriptor a_hpp = add_vertex({"a.hpp", 1000}, g);
  const Graph::vertex_descriptor b_hpp = add_vertex({"b.hpp", 2000}, g);
  add_edge(main_cpp, a_hpp, {"\"a.hpp\""}, g);
  add_edge(main_cpp, b_hpp, {"\"b.hpp\""}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  TestCompilationDatabase db(working_directory);
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  std::string sources[] = {"main.cpp"};
  llvm::Expected<std::pair<Graph, std::vector<Graph::vertex_descriptor>>>
      results = build_graph::from_compilation_db(db, sources, fs);
  check_equal(g, results->first);
}

TEST(BuildGraphTest, DiamondIncludes) {
  Graph g;
  const Graph::vertex_descriptor main_cpp = add_vertex({"main.cpp", 100}, g);
  const Graph::vertex_descriptor a_hpp = add_vertex({"a.hpp", 1000}, g);
  const Graph::vertex_descriptor b_hpp = add_vertex({"b.hpp", 2000}, g);

  const std::string c_path =
      (std::filesystem::path("common") / "c.hpp").string();
  const Graph::vertex_descriptor c_hpp = add_vertex({c_path, 30000}, g);
  add_edge(main_cpp, a_hpp, {"\"a.hpp\""}, g);
  add_edge(main_cpp, b_hpp, {"\"b.hpp\""}, g);
  add_edge(a_hpp, c_hpp, {"\"" + c_path + "\""}, g);
  add_edge(b_hpp, c_hpp, {"\"" + c_path + "\""}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  TestCompilationDatabase db(working_directory);
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  std::string sources[] = {"main.cpp"};
  llvm::Expected<std::pair<Graph, std::vector<Graph::vertex_descriptor>>>
      results = build_graph::from_compilation_db(db, sources, fs);
  check_equal(g, results->first);
}
} // namespace
