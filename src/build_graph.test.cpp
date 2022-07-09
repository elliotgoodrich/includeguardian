#include "build_graph.hpp"

#include <llvm/Support/VirtualFileSystem.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <initializer_list>
#include <ostream>

using namespace IncludeGuardian;

namespace {

const auto B = boost::units::information::byte;

const bool external = true;
const bool not_external = false;

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
    file_contents += std::to_string(file.cost.file_size.value());
    file_contents += ")\n";
    file_contents += "#pragma override_token_count(";
    file_contents += std::to_string(file.cost.token_count);
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

template <typename T> void dump(T &out, const std::vector<include_edge> &v) {
  if (v.empty()) {
    out << "[]";
    return;
  }
  out << '[';
  for (std::size_t i = 0; i < v.size() - 1; ++i) {
    out << v[i] << ", ";
  }
  out << v.back() << ']';
}

MATCHER_P(GraphsAreEquivalent, expected, "Whether two graphs compare equal") {
  if (num_vertices(arg) != num_vertices(expected)) {
    *result_listener << "num_vertices(arg) != num_vertices(expected)"
                     << num_vertices(arg) << " != " << num_vertices(expected);
    return false;
  }

  // We need to use an `unordered_map` as we may build up our graph in the wrong
  // order that we encounter files during our C++ preprocessor step.
  std::unordered_map<std::string, Graph::vertex_descriptor> file_lookup(
      num_vertices(arg));

  for (const Graph::vertex_descriptor v :
       boost::make_iterator_range(vertices(arg))) {
    const bool is_new = file_lookup.emplace(arg[v].path.string(), v).second;
    if (!is_new) {
      *result_listener << "Duplicate path found " << arg[v].path;
      return false;
    }
  }

  for (const Graph::vertex_descriptor v :
       boost::make_iterator_range(vertices(expected))) {
    const auto it = file_lookup.find(expected[v].path.string());
    if (it == file_lookup.end()) {
      *result_listener << "Could not find " << expected[v].path;
      return false;
    }

    if (arg[it->second] != expected[v]) {
      *result_listener << "file_nodes do not compare equal " << arg[it->second]
                       << " != " << expected[v];
      return false;
    }

    const std::vector<include_edge> l_edges = get_out_edges(it->second, arg);
    const std::vector<include_edge> r_edges = get_out_edges(v, expected);
    if (l_edges != r_edges) {
      *result_listener << "out_edges are not the same ";
      dump(*result_listener, l_edges);
      *result_listener << " != ";
      dump(*result_listener, r_edges);
      return false;
    }
  }
  return true;
}

TEST(BuildGraphTest, SimpleGraph) {
  Graph g;
  add_vertex({"main.cpp", not_external, {1u, 100 * B}}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
}

TEST(BuildGraphTest, MultipleChildren) {
  Graph g;
  const Graph::vertex_descriptor main_cpp =
      add_vertex({"main.cpp", not_external, {1u, 100 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({"a.hpp", not_external, {2u, 1000 * B}}, g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex({"b.hpp", not_external, {4u, 2000 * B}}, g);
  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 1}, g);
  add_edge(main_cpp, b_hpp, {"\"b.hpp\"", 2}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
}

TEST(BuildGraphTest, DiamondIncludes) {
  Graph g;
  const Graph::vertex_descriptor main_cpp =
      add_vertex({"main.cpp", not_external, {1u, 100 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({"a.hpp", not_external, {2u, 1000 * B}}, g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex({"b.hpp", not_external, {4u, 2000 * B}}, g);

  const std::string c_path =
      (std::filesystem::path("common") / "c.hpp").string();
  const Graph::vertex_descriptor c_hpp =
      add_vertex({c_path, not_external, {8u, 30000 * B}}, g);
  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 1}, g);
  add_edge(main_cpp, b_hpp, {"\"b.hpp\"", 2}, g);
  add_edge(a_hpp, c_hpp, {"\"" + c_path + "\"", 1}, g);
  add_edge(b_hpp, c_hpp, {"\"" + c_path + "\"", 1}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
}

TEST(BuildGraphTest, MultipleSources) {
  Graph g;
  const Graph::vertex_descriptor main1_cpp =
      add_vertex({"main1.cpp", not_external, {1u, 100 * B}}, g);
  const Graph::vertex_descriptor main2_cpp =
      add_vertex({"main2.cpp", not_external, {2u, 150 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({"a.hpp", not_external, {4u, 1000 * B}}, g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex({"b.hpp", not_external, {8u, 2000 * B}}, g);

  add_edge(main1_cpp, a_hpp, {"\"a.hpp\"", 1}, g);
  add_edge(main2_cpp, a_hpp, {"\"a.hpp\"", 1}, g);
  add_edge(a_hpp, b_hpp, {"\"b.hpp\"", 1}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
}

TEST(BuildGraphTest, DifferentDirectories) {
  Graph g;
  const std::filesystem::path src = "src";
  const std::filesystem::path include = "include";
  const Graph::vertex_descriptor main_cpp =
      add_vertex({src / "main1.cpp", not_external, {1u, 100 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({src / include / "a.hpp", not_external, {2u, 1000 * B}}, g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex({src / "b.hpp", not_external, {4u, 2000 * B}}, g);

  add_edge(main_cpp, a_hpp, {"\"include/a.hpp\"", 1}, g);
  add_edge(main_cpp, a_hpp, {"\"b.hpp\"", 2}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
}

TEST(BuildGraphTest, ExternalCode) {
  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path src = "src";
  const std::filesystem::path other = "other";
  const std::filesystem::path include = "include";
  const std::filesystem::path working_directory = root / "working_dir";
  fs->addFile((working_directory / src / "main1.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#include \"a.hpp\"\n"
                  "#include <b.hpp>\n"
                  "#pragma override_file_size(123)\n"
                  "#pragma override_token_count(1)\n"));
  fs->addFile((working_directory / other / "a.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#pragma override_file_size(246)\n"
                  "#pragma override_token_count(2)\n"));
  fs->addFile((working_directory / other / include / "b.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#pragma override_file_size(4812)\n"
                  "#pragma override_token_count(4)\n"));

  Graph g;
  const Graph::vertex_descriptor main_cpp =
      add_vertex({"main1.cpp", not_external, {1u, 123 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({"a.hpp", external, {2u, 246 * B}}, g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex({"b.hpp", external, {4u, 4812 * B}}, g);

  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 1}, g);
  add_edge(main_cpp, a_hpp, {"<b.hpp>", 2}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory / src,
      {working_directory / other, working_directory / other / include}, fs,
      get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
}

TEST(BuildGraphTest, UnremovableHeaders) {
  Graph g;
  const std::filesystem::path include = "include";
  const Graph::vertex_descriptor a_cpp =
      add_vertex({"a.cpp", not_external, {1u, 100 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({"a.hpp", not_external, {2u, 1000 * B}}, g);
  const Graph::vertex_descriptor b_cpp =
      add_vertex({"b.cpp", not_external, {4u, 100 * B}}, g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex({include / "b.hpp", not_external, {8u, 2000 * B}}, g);
  add_edge(a_cpp, a_hpp, {"\"a.hpp\"", 1, true}, g);
  add_edge(b_cpp, b_hpp, {"\"include/b.hpp\"", 1, true}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
}

} // namespace
