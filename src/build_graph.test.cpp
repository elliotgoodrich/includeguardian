#include "build_graph.hpp"

#include <llvm/Support/VirtualFileSystem.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <initializer_list>
#include <ostream>

using namespace IncludeGuardian;

namespace {

using namespace testing;

const auto B = boost::units::information::byte;

const bool external = true;
const bool not_external = false;
const bool removable = true;
const bool not_removable = false;

const std::function<build_graph::file_type(std::string_view)> get_file_type =
    [](std::string_view file) {
      if (file.ends_with(".cpp")) {
        return build_graph::file_type::source;
      } else if (file.ends_with(".hpp")) {
        return build_graph::file_type::header;
      } else if (file.ends_with(".pch")) {
        return build_graph::file_type::precompiled_header;
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
    std::string file_contents = "#pragma once\n";
    const file_node &file = graph[v];
    for (const Graph::edge_descriptor &edge :
         boost::make_iterator_range(out_edges(v, graph))) {
      file_contents += "#include ";
      file_contents += graph[edge].code;
      file_contents += '\n';
    }
    file_contents += "#pragma override_file_size(";
    file_contents += std::to_string(file.underlying_cost.file_size.value());
    file_contents += ")\n";
    file_contents += "#pragma override_token_count(";
    file_contents += std::to_string(file.underlying_cost.token_count);
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

bool vertices_equal(const file_node &lhs, const Graph &lgraph,
                    const file_node &rhs, const Graph &rgraph) {
  if (lhs.path != rhs.path || lhs.is_external != rhs.is_external ||
      lhs.underlying_cost != rhs.underlying_cost ||
      lhs.internal_incoming != rhs.internal_incoming ||
      lhs.is_precompiled != rhs.is_precompiled) {
    return false;
  }

  if (lhs.component.has_value() != rhs.component.has_value()) {
    return false;
  }

  if (!lhs.component.has_value()) {
    return true;
  }

  if (lgraph[*lhs.component].path != rgraph[*rhs.component].path) {
    return false;
  }

  return true;
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

    if (!vertices_equal(arg[it->second], arg, expected[v], expected)) {
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
  add_vertex({"main.cpp", not_external, 0u, {1, 100 * B}}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST(BuildGraphTest, FileStats) {
  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path working_directory = root / "working_dir";
  const std::string_view main_cpp_code =
      "#define SUM 1+1+1+1+1+1+1+1+1\n"
      "#define DEFINE_FOO int foo(int, int, int)\n"
      "DEFINE_FOO;DEFINE_FOO;\n"
      "#include \"a.hpp\"\n"
      "int main() {\n"
      "    SUM;\n"
      "}\n";
  fs->addFile((working_directory / "main.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  const std::string_view a_hpp_code = "#pragma once\n"
                                      "#if 100 > 99\n"
                                      "    DEFINE_FOO;\n"
                                      "    DEFINE_FOO;\n"
                                      "#else\n";
  "    DEFINE_FOO;\n"
  "    DEFINE_FOO;\n"
  "    DEFINE_FOO;\n"
  "    DEFINE_FOO;\n"
  "    DEFINE_FOO;\n"
  "    DEFINE_FOO;\n"
  "    DEFINE_FOO;\n"
  "#endif\n";
  fs->addFile((working_directory / "a.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(a_hpp_code));

  Graph g;
  const Graph::vertex_descriptor main_cpp = add_vertex(
      {"main.cpp", not_external, 0u, {25, main_cpp_code.size() * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({"a.hpp", not_external, 1u, {40, a_hpp_code.size() * B}}, g);

  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 4}, g);

  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST(BuildGraphTest, MultipleChildren) {
  Graph g;
  const Graph::vertex_descriptor main_cpp =
      add_vertex({"main.cpp", not_external, 0u, {1, 100 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({"a.hpp", not_external, 1u, {2, 1000 * B}}, g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex({"b.hpp", not_external, 1u, {4, 2000 * B}}, g);
  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(main_cpp, b_hpp, {"\"b.hpp\"", 3}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST(BuildGraphTest, DiamondIncludes) {
  Graph g;
  const Graph::vertex_descriptor main_cpp =
      add_vertex({"main.cpp", not_external, 0u, {1, 100 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({"a.hpp", not_external, 1u, {2, 1000 * B}}, g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex({"b.hpp", not_external, 1u, {4, 2000 * B}}, g);

  const std::string c_path =
      (std::filesystem::path("common") / "c.hpp").string();
  const Graph::vertex_descriptor c_hpp =
      add_vertex({c_path, not_external, 2u, {8, 30000 * B}}, g);
  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(main_cpp, b_hpp, {"\"b.hpp\"", 3}, g);
  add_edge(a_hpp, c_hpp, {"\"" + c_path + "\"", 2}, g);
  add_edge(b_hpp, c_hpp, {"\"" + c_path + "\"", 2}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST(BuildGraphTest, MultipleSources) {
  Graph g;
  const Graph::vertex_descriptor main1_cpp =
      add_vertex({"main1.cpp", not_external, 0u, {1, 100 * B}}, g);
  const Graph::vertex_descriptor main2_cpp =
      add_vertex({"main2.cpp", not_external, 0u, {2, 150 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({"a.hpp", not_external, 2u, {4, 1000 * B}}, g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex({"b.hpp", not_external, 1u, {8, 2000 * B}}, g);

  add_edge(main1_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(main2_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(a_hpp, b_hpp, {"\"b.hpp\"", 2}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST(BuildGraphTest, DifferentDirectories) {
  Graph g;
  const std::filesystem::path src = "src";
  const std::filesystem::path include = "include";
  const Graph::vertex_descriptor main_cpp =
      add_vertex({src / "main1.cpp", not_external, 0u, {1, 100 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({src / include / "a.hpp", not_external, 1u, {2, 1000 * B}}, g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex({src / "b.hpp", not_external, 1u, {4, 2000 * B}}, g);

  add_edge(main_cpp, a_hpp, {"\"include/a.hpp\"", 2}, g);
  add_edge(main_cpp, a_hpp, {"\"b.hpp\"", 3}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST(BuildGraphTest, ExternalCode) {
  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path src = "src";
  const std::filesystem::path other = "other";
  const std::filesystem::path sub = "sub";
  const std::filesystem::path include = "include";
  const std::filesystem::path working_directory = root / "working_dir";
  fs->addFile((working_directory / src / "main1.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#include \"sub/a.hpp\"\n"
                  "#include <b.hpp>\n"
                  "#pragma override_file_size(123)\n"
                  "#pragma override_token_count(1)\n"));
  fs->addFile((working_directory / other / sub / "a.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#pragma once\n"
                  "#include \"a_next.hpp\"\n"
                  "#pragma override_file_size(246)\n"
                  "#pragma override_token_count(2)\n"));
  fs->addFile((working_directory / other / sub / "a_next.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#pragma once\n"
                  "#pragma override_file_size(99)\n"
                  "#pragma override_token_count(99)\n"));
  fs->addFile((working_directory / other / include / "b.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#pragma once\n"
                  "#pragma override_file_size(4812)\n"
                  "#pragma override_token_count(4)\n"));

  Graph g;
  const Graph::vertex_descriptor main_cpp =
      add_vertex({"main1.cpp", not_external, 0u, {1, 123 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({sub / "a.hpp", external, 1u, {2, 246 * B}}, g);
  const Graph::vertex_descriptor a_next_hpp =
      add_vertex({sub / "a_next.hpp", external, 0u, {99, 99 * B}}, g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex({"b.hpp", external, 1u, {4, 4812 * B}}, g);

  add_edge(main_cpp, a_hpp, {"\"sub/a.hpp\"", 1}, g);
  add_edge(a_hpp, a_next_hpp, {"\"a_next.hpp\"", 2}, g);
  add_edge(main_cpp, a_hpp, {"<b.hpp>", 2}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory / src,
      {working_directory / other, working_directory / other / include}, fs,
      get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST(BuildGraphTest, UnremovableHeaders) {
  Graph g;
  const std::filesystem::path include = "include";
  const Graph::vertex_descriptor a_cpp =
      add_vertex({"a.cpp", not_external, 0u, {1, 100 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({"a.hpp", not_external, 1u, {2, 1000 * B}}, g);
  const Graph::vertex_descriptor b_cpp =
      add_vertex({"b.cpp", not_external, 0u, {4, 100 * B}}, g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex({include / "b.hpp", not_external, 1u, {8, 2000 * B}}, g);

  const Graph::edge_descriptor a_to_a =
      add_edge(a_cpp, a_hpp, {"\"a.hpp\"", 2, not_removable}, g).first;
  const Graph::edge_descriptor b_to_b =
      add_edge(b_cpp, b_hpp, {"\"include/b.hpp\"", 2, not_removable}, g).first;

  g[a_hpp].component = a_cpp;
  g[a_cpp].component = a_hpp;
  g[b_hpp].component = b_cpp;
  g[b_cpp].component = b_hpp;

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST(BuildGraphTest, PrecompiledHeaders) {
  Graph g;
  const bool precompiled = true;
  const std::filesystem::path include = "include";
  const Graph::vertex_descriptor a_cpp =
      add_vertex({"a.cpp", not_external, 0u, {1, 100 * B}}, g);
  const Graph::vertex_descriptor all_pch = add_vertex(
      {"all.pch", not_external, 1u, {2, 1000 * B}, std::nullopt, precompiled},
      g);
  const Graph::vertex_descriptor normal_h = add_vertex(
      {"normal.h", not_external, 1u, {3, 10000 * B}, std::nullopt, precompiled},
      g);
  add_edge(a_cpp, all_pch, {"\"all.pch\"", 2, removable}, g);
  add_edge(all_pch, normal_h, {"\"normal.h\"", 2, removable}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST(BuildGraphTest, ForcedIncludes) {
  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path working_directory = root / "working_dir";
  const std::filesystem::path foo = "foo";
  fs->addFile((working_directory / "main.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#include \"include.hpp\"\n"
                  "#pragma override_file_size(123)\n"
                  "#pragma override_token_count(1)\n"));
  fs->addFile((working_directory / "include.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#pragma override_file_size(246)\n"
                  "#pragma override_token_count(2)\n"));
  fs->addFile((working_directory / foo / "forced.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#include \"../foo/forced_sub.hpp\"\n"
                  "#pragma override_file_size(999)\n"
                  "#pragma override_token_count(4)\n"));
  fs->addFile((working_directory / foo / "forced_sub.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#pragma override_file_size(1000)\n"
                  "#pragma override_token_count(8)\n"));
  Graph g;
  const Graph::vertex_descriptor main_cpp =
      add_vertex({"main.cpp", not_external, 0u, {1, 123 * B}}, g);
  const Graph::vertex_descriptor include_hpp =
      add_vertex({"include.hpp", not_external, 1u, {2, 246 * B}}, g);
  const Graph::vertex_descriptor forced_hpp = add_vertex(
      {working_directory / foo / "forced.hpp", not_external, 1u, {4, 999 * B}},
      g);
  const Graph::vertex_descriptor forced_sub_hpp =
      add_vertex({working_directory / foo / "forced_sub.hpp",
                  not_external,
                  1u,
                  {8, 1000 * B}},
                 g);
  add_edge(main_cpp, forced_hpp,
           {"\"C:\\working_dir\\foo\\forced.hpp\"", 0, not_removable}, g);
  add_edge(main_cpp, include_hpp, {"\"include.hpp\"", 1, removable}, g);
  add_edge(forced_hpp, forced_sub_hpp,
           {"\"../foo/forced_sub.hpp\"", 1, removable}, g);

  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type,
                            {working_directory / foo / "forced.hpp"});
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files,
              UnorderedElementsAre(forced_hpp, forced_sub_hpp, include_hpp));
}

TEST(BuildGraphTest, XMacros) {
  // Test files that are unguarded and can be included multiple times
  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path working_directory = root / "working_dir";
  const std::string_view main_cpp_code =
      "#include \"x_macro.hpp\"\n"
      "#define X(name) \"#name\"\n"
      "const char[] all = FRUITS;\n"
      "#undef X\n"
      "#include \"a.hpp\"\n"
      "int main() {\n"
      "#define X(name) const char *name = \"#name\";\n"
      "{\n"
      "    FRUITS\n"
      "}\n"
      "{\n"
      "    FRUITS\n"
      "}\n"
      "#undef X\n"
      "}\n";
  fs->addFile((working_directory / "main.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  const std::string_view a_hpp_code = "#pragma once\n"
                                      "#include \"x_macro.hpp\"\n"
                                      "enum class Fruits {\n"
                                      "#define X(name) name,\n"
                                      "}\n"
                                      "#undef X\n";
  fs->addFile((working_directory / "a.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(a_hpp_code));
  const std::string_view x_macro_hpp_code = "#define FRUITS \\\n"
                                            "X(apple) \\\n"
                                            "X(pear) \\\n"
                                            "X(blueberry) \\\n"
                                            "X(lemon) \\\n"
                                            "X(peach) \\\n"
                                            "X(orange) \\\n"
                                            "X(banana) \\n";
  fs->addFile((working_directory / "x_macro.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(x_macro_hpp_code));

  Graph g;
  const Graph::vertex_descriptor main_cpp =
      add_vertex({"main.cpp",
                  not_external,
                  0u,
                  {113, (main_cpp_code.size() + x_macro_hpp_code.size()) * B}},
                 g);
  const Graph::vertex_descriptor x_macro_hpp =
      add_vertex({"x_macro.hpp", not_external, 2u, {0, 0.0 * B}}, g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex({"a.hpp",
                  not_external,
                  1u,
                  {21, (a_hpp_code.size() + x_macro_hpp_code.size()) * B}},
                 g);

  add_edge(main_cpp, x_macro_hpp, {"\"x_macro.hpp\"", 1}, g);
  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 5}, g);
  add_edge(a_hpp, x_macro_hpp, {"\"x_macro.hpp\"", 2}, g);

  llvm::Expected<build_graph::result> results =
      build_graph::from_dir(working_directory, {}, fs, get_file_type);
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, UnorderedElementsAre(x_macro_hpp));
}

} // namespace
