#include "build_graph.hpp"

#include <clang/Tooling/CompilationDatabase.h>

#include <llvm/Support/VirtualFileSystem.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <initializer_list>
#include <ostream>

using namespace IncludeGuardian;

namespace {

const auto B = boost::units::information::byte;

const std::function<build_graph::file_type(std::string_view)> get_file_type =
    [](std::string_view file) {
      if (file.ends_with(".cpp")) {
        return build_graph::file_type::source;
      } else if (file.ends_with(".hpp")) {
        return build_graph::file_type::header;
      } else {
        return build_graph::file_type::ignore;
      }
    };

// Needs to be fixed on non-window's systems
static const std::filesystem::path root = "C:\\";

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
    file_contents += std::to_string(file.file_size.value());
    file_contents += ")\n";
    const std::filesystem::path p = working_directory / file.path;
    fs->addFile(p.string(), 0,
                llvm::MemoryBuffer::getMemBufferCopy(file_contents));
  }
  return fs;
}

std::vector<include_edge> get_out_edges(Graph::vertex_descriptor v,
                                        const Graph &g) {
  std::vector<include_edge> out;
  const auto [edge_start, edge_end] = out_edges(v, g);
  std::transform(edge_start, edge_end, std::back_inserter(out),
                 [&g](Graph::edge_descriptor e) { return g[e]; });
  return out;
}

void check_equal(const Graph &actual, const Graph &expected) {
  ASSERT_EQ(num_vertices(actual), num_vertices(expected));

  // We need to use an `unordered_map` as we may build up our graph in the wrong
  // order that we encounter files during our C++ preprocessor step.
  std::unordered_map<std::string, Graph::vertex_descriptor> file_lookup(
      num_vertices(actual));

  for (const Graph::vertex_descriptor v :
       boost::make_iterator_range(vertices(actual))) {
    const bool is_new = file_lookup.emplace(actual[v].path.string(), v).second;
    EXPECT_TRUE(is_new);
  }

  for (const Graph::vertex_descriptor v :
       boost::make_iterator_range(vertices(expected))) {
    const auto it = file_lookup.find(expected[v].path.string());
    ASSERT_NE(it, file_lookup.end()) << "Could not find " << expected[v].path;

    EXPECT_EQ(get_out_edges(it->second, actual), get_out_edges(v, expected))
        << "from " << expected[v].path;
  }
}

TEST(BuildGraphTest, SimpleGraph) {
  Graph g;
  add_vertex({"main.cpp", 100 * B}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory.string(), {}, fs, get_file_type);
  check_equal(g, results->graph);
}

TEST(BuildGraphTest, MultipleChildren) {
  Graph g;
  const Graph::vertex_descriptor main_cpp =
      add_vertex({"main.cpp", 100 * B}, g);
  const Graph::vertex_descriptor a_hpp = add_vertex({"a.hpp", 1000 * B}, g);
  const Graph::vertex_descriptor b_hpp = add_vertex({"b.hpp", 2000 * B}, g);
  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 1}, g);
  add_edge(main_cpp, b_hpp, {"\"b.hpp\"", 2}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory.string(), {}, fs, get_file_type);
  check_equal(g, results->graph);
}

TEST(BuildGraphTest, DiamondIncludes) {
  Graph g;
  const Graph::vertex_descriptor main_cpp =
      add_vertex({"main.cpp", 100 * B}, g);
  const Graph::vertex_descriptor a_hpp = add_vertex({"a.hpp", 1000 * B}, g);
  const Graph::vertex_descriptor b_hpp = add_vertex({"b.hpp", 2000 * B}, g);

  const std::string c_path =
      (std::filesystem::path("common") / "c.hpp").string();
  const Graph::vertex_descriptor c_hpp = add_vertex({c_path, 30000 * B}, g);
  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 1}, g);
  add_edge(main_cpp, b_hpp, {"\"b.hpp\"", 2}, g);
  add_edge(a_hpp, c_hpp, {"\"" + c_path + "\"", 1}, g);
  add_edge(b_hpp, c_hpp, {"\"" + c_path + "\"", 1}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory.string(), {}, fs, get_file_type);
  check_equal(g, results->graph);
}

TEST(BuildGraphTest, MultipleSources) {
  Graph g;
  const Graph::vertex_descriptor main1_cpp =
      add_vertex({"main1.cpp", 100 * B}, g);
  const Graph::vertex_descriptor main2_cpp =
      add_vertex({"main2.cpp", 150 * B}, g);
  const Graph::vertex_descriptor a_hpp = add_vertex({"a.hpp", 1000 * B}, g);
  const Graph::vertex_descriptor b_hpp = add_vertex({"b.hpp", 2000 * B}, g);

  add_edge(main1_cpp, a_hpp, {"\"a.hpp\"", 1}, g);
  add_edge(main2_cpp, a_hpp, {"\"a.hpp\"", 1}, g);
  add_edge(a_hpp, b_hpp, {"\"b.hpp\"", 1}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory.string(), {}, fs, get_file_type);
  check_equal(g, results->graph);
}

TEST(BuildGraphTest, DifferentDirectories) {
  Graph g;
  const std::filesystem::path dir1 = "dir1";
  const Graph::vertex_descriptor main_cpp =
      add_vertex({dir1 / "main1.cpp", 100 * B}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({dir1 / "a.hpp", 1000 * B}, g);
  const Graph::vertex_descriptor b_hpp = add_vertex({"b.hpp", 2000 * B}, g);

  add_edge(main_cpp, a_hpp, {"\"../dir1/a.hpp\"", 1}, g);
  add_edge(main_cpp, a_hpp, {"\"../b.hpp\"", 2}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory.string(), {}, fs, get_file_type);
  check_equal(g, results->graph);
}
} // namespace
