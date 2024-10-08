#include "build_graph.hpp"

#include "matchers.hpp"

#include <llvm/Support/VirtualFileSystem.h>

#include <boost/predef.h>

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

class BuildGraphTest : public testing::TestWithParam<build_graph::options> {};

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

#if BOOST_OS_WINDOWS
static const std::filesystem::path root = "C:\\";
#else
static const std::filesystem::path root = "/home/";
#endif

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

/*
 Drop all trailing '_' characters at the end of all files that
 have been asked for.  This allows us to have multiple names to
 refer to the same file.
*/
class TrimFileSystem : public llvm::vfs::FileSystem {
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> m_underlying;

  static std::string trim(const llvm::Twine &path) {
    llvm::SmallVector<char, 256> buffer;
    std::string str(path.toStringRef(buffer).str());
    while (str.ends_with("_")) {
      str.pop_back();
    }
    return str;
  }

public:
  explicit TrimFileSystem(
      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> underlying)
      : m_underlying(std::move(underlying)) {}

  llvm::ErrorOr<llvm::vfs::Status> status(const llvm::Twine &path) final {
    return m_underlying->status(trim(path));
  }

  llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>>
  openFileForRead(const llvm::Twine &path) final {
    return m_underlying->openFileForRead(trim(path));
  }

  llvm::vfs::directory_iterator dir_begin(const llvm::Twine &Dir,
                                          std::error_code &EC) final {
    return m_underlying->dir_begin(Dir, EC);
  }
  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const final {
    return m_underlying->getCurrentWorkingDirectory();
  }
  std::error_code setCurrentWorkingDirectory(const llvm::Twine &Path) final {
    return m_underlying->setCurrentWorkingDirectory(Path);
  }
  std::error_code getRealPath(const llvm::Twine &Path,
                              llvm::SmallVectorImpl<char> &Output) const final {
    return m_underlying->getRealPath(Path, Output);
  }
  std::error_code isLocal(const llvm::Twine &Path, bool &Result) final {
    return m_underlying->isLocal(Path, Result);
  }

protected:
  FileSystem &getUnderlyingFS() { return *m_underlying; }

  virtual void anchor() {}
};

TEST_P(BuildGraphTest, SimpleGraph) {
  Graph g;
  add_vertex(file_node("main.cpp").with_cost(1, 100 * B), g);
  add_vertex(file_node("main_copy.cpp").with_cost(1, 101 * B), g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST_P(BuildGraphTest, FileStats) {
  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path working_directory = root / "working_dir";
  const std::string_view main_cpp_code =
      "#define SUM 1+1+1+1+1+1+1+1+1\n"
      "#define DEFINE_FOO int foo(int, int, int)\n"
      "DEFINE_FOO;DEFINE_FOO;\n"
      "#include \"a.hpp\"\n"
      "DEFINE_FOO;\n"
      "int main() {\n"
      "    SUM;\n"
      "}\n";
  fs->addFile((working_directory / "main.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  fs->addFile((working_directory / "main_copy.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  const std::string_view a_hpp_code = "#pragma once\n"
                                      "#if 100 > 99\n"
                                      "    DEFINE_FOO;\n"
                                      "    DEFINE_FOO;\n"
                                      "#else\n"
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
      file_node("main.cpp").with_cost(55, main_cpp_code.size() * B), g);
  const Graph::vertex_descriptor main_copy_cpp = add_vertex(
      file_node("main_copy.cpp").with_cost(55, main_cpp_code.size() * B), g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex(file_node("a.hpp")
                     .with_cost(20, a_hpp_code.size() * B)
                     .set_internal_parents(2),
                 g);

  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 4}, g);
  add_edge(main_copy_cpp, a_hpp, {"\"a.hpp\"", 4}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST_P(BuildGraphTest, MultipleChildren) {
  Graph g;
  const Graph::vertex_descriptor main_cpp =
      add_vertex(file_node("main.cpp").with_cost(1, 100 * B), g);
  const Graph::vertex_descriptor main_copy_cpp =
      add_vertex(file_node("main_copy.cpp").with_cost(1, 101 * B), g);
  const Graph::vertex_descriptor a_hpp = add_vertex(
      file_node("a.hpp").with_cost(2, 1000 * B).set_internal_parents(2), g);
  const Graph::vertex_descriptor b_hpp = add_vertex(
      file_node("b.hpp").with_cost(4, 2000 * B).set_internal_parents(2), g);
  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(main_cpp, b_hpp, {"\"b.hpp\"", 3}, g);
  add_edge(main_copy_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(main_copy_cpp, b_hpp, {"\"b.hpp\"", 3}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST_P(BuildGraphTest, DiamondIncludes) {
  Graph g;
  const Graph::vertex_descriptor main_cpp =
      add_vertex(file_node("main.cpp").with_cost(1, 100 * B), g);
  const Graph::vertex_descriptor main_copy_cpp =
      add_vertex(file_node("main_copy.cpp").with_cost(1, 101 * B), g);
  const Graph::vertex_descriptor a_hpp = add_vertex(
      file_node("a.hpp").with_cost(2, 1000 * B).set_internal_parents(2), g);
  const Graph::vertex_descriptor b_hpp = add_vertex(
      file_node("b.hpp").with_cost(4, 2000 * B).set_internal_parents(2), g);

  const std::string c_path =
      (std::filesystem::path("common") / "c.hpp").string();
  const Graph::vertex_descriptor c_hpp = add_vertex(
      file_node(c_path).with_cost(8, 30000 * B).set_internal_parents(2), g);
  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(main_cpp, b_hpp, {"\"b.hpp\"", 3}, g);
  add_edge(main_copy_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(main_copy_cpp, b_hpp, {"\"b.hpp\"", 3}, g);
  add_edge(a_hpp, c_hpp, {"\"" + c_path + "\"", 2}, g);
  add_edge(b_hpp, c_hpp, {"\"" + c_path + "\"", 2}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST_P(BuildGraphTest, MultipleSources) {
  Graph g;
  const Graph::vertex_descriptor main1_cpp = add_vertex(
      file_node("main1.cpp").with_cost(1, 100 * B).set_internal_parents(0), g);
  const Graph::vertex_descriptor main1_copy_cpp = add_vertex(
      file_node("main1_copy.cpp").with_cost(1, 101 * B).set_internal_parents(0),
      g);
  const Graph::vertex_descriptor main2_cpp = add_vertex(
      file_node("main2.cpp").with_cost(2, 150 * B).set_internal_parents(0), g);
  const Graph::vertex_descriptor main2_copy_cpp = add_vertex(
      file_node("main2_copy.cpp").with_cost(2, 151 * B).set_internal_parents(0),
      g);
  const Graph::vertex_descriptor a_hpp = add_vertex(
      file_node("a.hpp").with_cost(4, 1000 * B).set_internal_parents(4), g);
  const Graph::vertex_descriptor b_hpp = add_vertex(
      file_node("b.hpp").with_cost(8, 2000 * B).set_internal_parents(1), g);

  add_edge(main1_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(main1_copy_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(main2_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(main2_copy_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(a_hpp, b_hpp, {"\"b.hpp\"", 2}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST_P(BuildGraphTest, DifferentDirectories) {
  Graph g;
  const std::filesystem::path src = "src";
  const std::filesystem::path include = "include";
  const Graph::vertex_descriptor main_cpp =
      add_vertex(file_node(src / "main1.cpp")
                     .with_cost(1, 100 * B)
                     .set_internal_parents(0),
                 g);
  const Graph::vertex_descriptor main_copy_cpp =
      add_vertex(file_node(src / "main_copy.cpp")
                     .with_cost(1, 101 * B)
                     .set_internal_parents(0),
                 g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex(file_node(src / include / "a.hpp")
                     .with_cost(2, 1000 * B)
                     .set_internal_parents(2),
                 g);
  const Graph::vertex_descriptor b_hpp = add_vertex(
      file_node(src / "b.hpp").with_cost(4, 2000 * B).set_internal_parents(2),
      g);

  add_edge(main_cpp, a_hpp, {"\"include/a.hpp\"", 2}, g);
  add_edge(main_cpp, b_hpp, {"\"b.hpp\"", 3}, g);
  add_edge(main_copy_cpp, a_hpp, {"\"include/a.hpp\"", 2}, g);
  add_edge(main_copy_cpp, b_hpp, {"\"b.hpp\"", 3}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST_P(BuildGraphTest, ExternalCode) {
  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path src = "src";
  const std::filesystem::path other = "other";
  const std::filesystem::path sub = "sub";
  const std::filesystem::path include = "include";
  const std::filesystem::path ours = "ours";
  const std::filesystem::path working_directory = root / "working_dir";
  const std::string_view main_cpp_code = "#include \"sub/a.hpp\"\n"
                                         "#include <b.hpp>\n"
                                         "#include \"internal.hpp\"\n"
                                         "#pragma override_file_size(123)\n"
                                         "#pragma override_token_count(1)\n";
  fs->addFile((working_directory / src / "main1.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  fs->addFile((working_directory / src / "main1_copy.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
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
  fs->addFile((working_directory / other / ours / "internal.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#pragma once\n"
                  "#pragma override_file_size(999)\n"
                  "#pragma override_token_count(999)\n"));

  Graph g;
  const Graph::vertex_descriptor main_cpp = add_vertex(
      file_node("main1.cpp").with_cost(1, 123 * B).set_internal_parents(0), g);
  const Graph::vertex_descriptor main_copy_cpp = add_vertex(
      file_node("main1_copy.cpp").with_cost(1, 123 * B).set_internal_parents(0),
      g);
  const Graph::vertex_descriptor a_hpp = add_vertex(file_node(sub / "a.hpp")
                                                        .with_cost(2, 246 * B)
                                                        .set_internal_parents(2)
                                                        .set_external(true),
                                                    g);
  const Graph::vertex_descriptor a_next_hpp =
      add_vertex(file_node(sub / "a_next.hpp")
                     .with_cost(99, 99 * B)
                     .set_external_parents(1)
                     .set_external(true),
                 g);
  const Graph::vertex_descriptor b_hpp = add_vertex(file_node("b.hpp")
                                                        .with_cost(4, 4812 * B)
                                                        .set_internal_parents(2)
                                                        .set_external(true),
                                                    g);
  const Graph::vertex_descriptor internal_hpp = add_vertex(
      file_node("internal.hpp").with_cost(999, 999 * B).set_internal_parents(2),
      g);

  add_edge(main_cpp, a_hpp, {"\"sub/a.hpp\"", 1}, g);
  add_edge(main_copy_cpp, a_hpp, {"\"sub/a.hpp\"", 1}, g);
  add_edge(a_hpp, a_next_hpp, {"\"a_next.hpp\"", 2}, g);
  add_edge(main_cpp, b_hpp, {"<b.hpp>", 2}, g);
  add_edge(main_copy_cpp, b_hpp, {"<b.hpp>", 2}, g);
  add_edge(main_cpp, internal_hpp, {"\"internal.hpp\"", 3}, g);
  add_edge(main_copy_cpp, internal_hpp, {"\"internal.hpp\"", 3}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory / src,
      {{working_directory / other, clang::SrcMgr::C_System},
       {working_directory / other / include, clang::SrcMgr::C_System},
       {working_directory / other / ours, clang::SrcMgr::C_User}},
      fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST_P(BuildGraphTest, UnremovableHeaders) {
  Graph g;
  const std::filesystem::path include = "include";
  const Graph::vertex_descriptor a_cpp = add_vertex(
      file_node("a.cpp").with_cost(1, 100 * B).set_internal_parents(0), g);
  const Graph::vertex_descriptor a_hpp = add_vertex(
      file_node("a.hpp").with_cost(2, 1000 * B).set_internal_parents(1), g);
  const Graph::vertex_descriptor b_cpp = add_vertex(
      file_node("b.cpp").with_cost(4, 100 * B).set_internal_parents(0), g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex(file_node(include / "b.hpp")
                     .with_cost(8, 2000 * B)
                     .set_internal_parents(1),
                 g);

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
  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST_P(BuildGraphTest, PrecompiledHeaders) {
  Graph g;
  const std::filesystem::path include = "include";
  const Graph::vertex_descriptor a_cpp = add_vertex(
      file_node("a.cpp").with_cost(1, 100 * B).set_internal_parents(0), g);
  const Graph::vertex_descriptor all_pch =
      add_vertex(file_node("all.pch")
                     .with_cost(2, 1000 * B)
                     .set_internal_parents(1)
                     .set_precompiled(true),
                 g);
  const Graph::vertex_descriptor normal_h =
      add_vertex(file_node("normal.h")
                     .with_cost(3, 10000 * B)
                     .set_internal_parents(1)
                     .set_precompiled(true),
                 g);
  add_edge(a_cpp, all_pch, {"\"all.pch\"", 2, removable}, g);
  add_edge(all_pch, normal_h, {"\"normal.h\"", 2, removable}, g);

  const std::filesystem::path working_directory = root / "working_dir";
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs =
      make_file_system(g, working_directory);
  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

TEST_P(BuildGraphTest, ForcedIncludes) {
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
                  "#pragma once\n"
                  "#pragma override_file_size(246)\n"
                  "#pragma override_token_count(2)\n"));
  const std::string_view forced_hpp_code =
      "#pragma once\n"
      "#include \"../foo/forced_sub.hpp\"\n"
      "int myFunction(int a) {\n"
      "    return -a + 1;\n"
      "}\n";
  fs->addFile((working_directory / foo / "forced.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(forced_hpp_code));
  fs->addFile((working_directory / foo / "forced_sub.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#pragma override_file_size(1000)\n"
                  "#pragma override_token_count(8)\n"));
  Graph g;
  const Graph::vertex_descriptor main_cpp = add_vertex(
      file_node("main.cpp").with_cost(1, 123 * B).set_internal_parents(0), g);
  const Graph::vertex_descriptor include_hpp = add_vertex(
      file_node("include.hpp").with_cost(2, 246 * B).set_internal_parents(1),
      g);
  const Graph::vertex_descriptor forced_hpp =
      add_vertex(file_node(working_directory / foo / "forced.hpp")
                     .with_cost(22, (1000 + forced_hpp_code.size()) * B)
                     .set_internal_parents(1),
                 g);
  const Graph::vertex_descriptor forced_sub_hpp =
      add_vertex(file_node(working_directory / foo / "forced_sub.hpp")
                     .with_cost(0, 0 * B)
                     .set_internal_parents(1),
                 g);
  const std::string expected = [&] {
    std::ostringstream ss;
    ss << (working_directory / foo / "forced.hpp");
    return ss.str();
  }();
  add_edge(main_cpp, forced_hpp, {expected, 0, not_removable}, g);
  add_edge(main_cpp, include_hpp, {"\"include.hpp\"", 1, removable}, g);
  add_edge(forced_hpp, forced_sub_hpp,
           {"\"../foo/forced_sub.hpp\"", 2, removable}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam(),
      {working_directory / foo / "forced.hpp"});
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files,
              UnorderedElementsAre(VertexDescriptorIs(
                  results->graph,
                  Field(&file_node::path,
                        Eq(working_directory / foo / "forced_sub.hpp")))));
}

TEST_P(BuildGraphTest, XMacros) {
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
  fs->addFile((working_directory / "main_copy.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  const std::string_view a_hpp_code = "#pragma once\n"
                                      "#include \"x_macro.hpp\"\n"
                                      "enum class Fruits {\n"
                                      "#define X(name) name,\n"
                                      "FRUITS\n"
                                      "#undef X\n"
                                      "}\n"
                                      "#undef X\n";
  fs->addFile((working_directory / "a.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(a_hpp_code));
  const std::string_view something_hpp_code = "#define EXTRA peach\n";
  fs->addFile((working_directory / "something.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(something_hpp_code));
  const std::string_view x_macro_hpp_code = "#include \"something.hpp\"\n"
                                            "#define FRUITS \\\n"
                                            "X(apple) \\\n"
                                            "X(pear) \\\n"
                                            "X(blueberry) \\\n"
                                            "X(lemon) \\\n"
                                            "X(EXTRA) \\\n"
                                            "X(orange) \\\n"
                                            "X(banana) \\n";
  fs->addFile((working_directory / "x_macro.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(x_macro_hpp_code));

  Graph g;
  const Graph::vertex_descriptor main_cpp = add_vertex(
      file_node("main.cpp")
          .with_cost(129, (main_cpp_code.size() + x_macro_hpp_code.size() +
                           something_hpp_code.size()) *
                              B)
          .set_internal_parents(0),
      g);
  const Graph::vertex_descriptor main_copy_cpp = add_vertex(
      file_node("main_copy.cpp")
          .with_cost(129, (main_cpp_code.size() + x_macro_hpp_code.size() +
                           something_hpp_code.size()) *
                              B)
          .set_internal_parents(0),
      g);
  const Graph::vertex_descriptor something_hpp = add_vertex(
      file_node("something.hpp").with_cost(0, 0.0 * B).set_internal_parents(1),
      g);
  const Graph::vertex_descriptor x_macro_hpp = add_vertex(
      file_node("x_macro.hpp").with_cost(0, 0.0 * B).set_internal_parents(3),
      g);
  const Graph::vertex_descriptor a_hpp = add_vertex(
      file_node("a.hpp")
          .with_cost(21, (a_hpp_code.size() + x_macro_hpp_code.size() +
                          something_hpp_code.size()) *
                             B)
          .set_internal_parents(2),
      g);

  add_edge(main_cpp, x_macro_hpp, {"\"x_macro.hpp\"", 1}, g);
  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 5}, g);
  add_edge(main_copy_cpp, x_macro_hpp, {"\"x_macro.hpp\"", 1}, g);
  add_edge(main_copy_cpp, a_hpp, {"\"a.hpp\"", 5}, g);
  add_edge(a_hpp, x_macro_hpp, {"\"x_macro.hpp\"", 2}, g);
  add_edge(x_macro_hpp, something_hpp, {"\"something.hpp\"", 1}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(
      results->unguarded_files,
      UnorderedElementsAre(
          VertexDescriptorIs(results->graph,
                             Field(&file_node::path, Eq("something.hpp"))),
          VertexDescriptorIs(results->graph,
                             Field(&file_node::path, Eq("x_macro.hpp")))));
}

TEST_P(BuildGraphTest, NaughtyIncludes) {
  // Test files that change their include size when included by different
  // sources.  This is allowed by non-guarded files, but for guarded files
  // we should report an error.
  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path working_directory = root / "working_dir";
  const std::string_view main_cpp_code = "#include \"unguarded.hpp\"\n"
                                         "#define USE_A\n"
                                         "#include \"unguarded.hpp\"\n";
  fs->addFile((working_directory / "main.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  fs->addFile((working_directory / "main_copy.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  const std::string_view unguarded_hpp_code = "#ifdef USE_A\n"
                                              "  #include \"a.hpp\"\n"
                                              "#else\n"
                                              "  #include \"b.hpp\"\n"
                                              "#endif\n";
  fs->addFile((working_directory / "unguarded.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(unguarded_hpp_code));
  fs->addFile((working_directory / "a.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#pragma override_file_size(1000)\n"
                  "#pragma override_token_count(223)\n"));
  fs->addFile((working_directory / "b.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(
                  "#pragma override_file_size(555)\n"
                  "#pragma override_token_count(111)\n"));

  Graph g;
  const Graph::vertex_descriptor main_cpp = add_vertex(
      file_node("main.cpp")
          .with_cost(224, (main_cpp_code.size() +
                           2 * unguarded_hpp_code.size() + 1000 + 555) *
                              B)
          .set_internal_parents(0),
      g);
  const Graph::vertex_descriptor main_copy_cpp = add_vertex(
      file_node("main_copy.cpp")
          .with_cost(224, (main_cpp_code.size() +
                           2 * unguarded_hpp_code.size() + 1000 + 555) *
                              B)
          .with_cost(224, 1747 * B)
          .set_internal_parents(0),
      g);
  const Graph::vertex_descriptor unguarded_hpp = add_vertex(
      file_node("unguarded.hpp").with_cost(0, 0.0 * B).set_internal_parents(2),
      g);
  const Graph::vertex_descriptor a_hpp = add_vertex(
      file_node("a.hpp").with_cost(0, 0.0 * B).set_internal_parents(1), g);
  const Graph::vertex_descriptor b_hpp = add_vertex(
      file_node("b.hpp").with_cost(0, 0.0 * B).set_internal_parents(1), g);

  add_edge(main_cpp, unguarded_hpp, {"\"unguarded.hpp\"", 1}, g);
  add_edge(main_copy_cpp, unguarded_hpp, {"\"unguarded.hpp\"", 1}, g);
  add_edge(unguarded_hpp, b_hpp, {"\"b.hpp\"", 4}, g);
  add_edge(unguarded_hpp, a_hpp, {"\"a.hpp\"", 2}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(
      results->unguarded_files,
      UnorderedElementsAre(
          VertexDescriptorIs(results->graph,
                             Field(&file_node::path, Eq("unguarded.hpp"))),
          VertexDescriptorIs(results->graph,
                             Field(&file_node::path, Eq("a.hpp"))),
          VertexDescriptorIs(results->graph,
                             Field(&file_node::path, Eq("b.hpp")))));
}

// There is an interesting assert that occurs only during unit tests
// when we have a file `idempotent.hpp` that would be identical
// to the `enable_replace_file_optimization` replacement.  This is
// because in both unit tests and when doing the optimization we
// use `InMemoryFileSystem`.  This generates the `UniqueID` for the
// file based on its file and content, so in our case we would get
// identical `UniqueID` for the original and replacement, even though
// they occur in different `InMemoryFileSystem` instances.  This hits
// an assert in our code `assert(new_id != id)` in `build_graph.cpp`
// where we want to check that the replacement has occurred.
//
// This is "fixed" by putting a comment at the top of each replacement
// file that shouldn't occur during unit tests.
TEST_P(BuildGraphTest, IdempotentFile) {
  const std::string_view idempotent_hpp_code = "#pragma once\n"
  "#define FOO 1\n";
  const std::string_view main_cpp_code = "#include \"idempotent.hpp\"\n";

  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path working_directory = root / "working_dir";
  fs->addFile((working_directory / "idempotent.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(idempotent_hpp_code));
  fs->addFile((working_directory / "main.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  fs->addFile((working_directory / "main2.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));

  Graph g;
  const Graph::vertex_descriptor idempotent_hpp =
      add_vertex(file_node("idempotent.hpp")
                     .with_cost(0, idempotent_hpp_code.size() * B)
                     .set_internal_parents(2),
                 g);
  const Graph::vertex_descriptor main_cpp = add_vertex(
      file_node("main.cpp").with_cost(1, main_cpp_code.size() * B), g);
  const Graph::vertex_descriptor main2_cpp = add_vertex(
      file_node("main2.cpp").with_cost(1, main_cpp_code.size() * B), g);

  add_edge(main_cpp, idempotent_hpp, {"\"idempotent.hpp\"", 1}, g);
  add_edge(main2_cpp, idempotent_hpp, {"\"idempotent.hpp\"", 1}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

// We want to test that for our generated files:
//   - Any defines are kept
//   - Guarded files are still guarded
//   - Unguarded files are not guarded
//   - Included files are still included
TEST_P(BuildGraphTest, ReducedFileOptimization) {
  const std::string_view good_file_hpp_code = "#pragma once\n"
                                              "#define GOOD_FILE\n";
  const std::string_view common_hpp_code = "#pragma once\n"
                                           "#define FOO BAR\n";
  const std::string_view guarded_hpp_code = "#pragma once\n"
                                            "#define BAR 1\n"
                                            "#include \"common.hpp\"\n"
                                            "#if FOO == 1\n"
                                            "  #include \"good_file.hpp\"\n"
                                            "#else\n"
                                            "  #include \"bad_file.hpp\"\n"
                                            "#endif\n";
  const std::string_view main_cpp_code = "#include \"guarded.hpp\"\n";

  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path working_directory = root / "working_dir";
  fs->addFile((working_directory / "good_file.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(good_file_hpp_code));
  fs->addFile((working_directory / "common.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(common_hpp_code));
  fs->addFile((working_directory / "guarded.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(guarded_hpp_code));
  fs->addFile((working_directory / "main.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  fs->addFile((working_directory / "main2.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));

  Graph g;
  const Graph::vertex_descriptor good_file_hpp =
      add_vertex(file_node("good_file.hpp")
                     .with_cost(0, good_file_hpp_code.size() * B)
                     .set_internal_parents(1),
                 g);
  const Graph::vertex_descriptor common_hpp =
      add_vertex(file_node("common.hpp")
                     .with_cost(0, common_hpp_code.size() * B)
                     .set_internal_parents(1),
                 g);
  const Graph::vertex_descriptor guarded_hpp =
      add_vertex(file_node("guarded.hpp")
                     .with_cost(0, guarded_hpp_code.size() * B)
                     .set_internal_parents(2),
                 g);
  const Graph::vertex_descriptor main_cpp = add_vertex(
      file_node("main.cpp").with_cost(1, main_cpp_code.size() * B), g);
  const Graph::vertex_descriptor main2_cpp = add_vertex(
      file_node("main2.cpp").with_cost(1, main_cpp_code.size() * B), g);

  add_edge(main_cpp, guarded_hpp, {"\"guarded.hpp\"", 1}, g);
  add_edge(main2_cpp, guarded_hpp, {"\"guarded.hpp\"", 1}, g);
  add_edge(guarded_hpp, common_hpp, {"\"common.hpp\"", 3}, g);
  add_edge(guarded_hpp, good_file_hpp, {"\"good_file.hpp\"", 5}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

// Want to check that includes outside of the include guard
// still cause a file to not be registered as "guarded" even
// if that include is guarded itself and encountered multiple
// times.
TEST_P(BuildGraphTest, OutsideIncludes) {
  const std::string_view common_hpp_code = "#ifndef COMMON_HPP\n"
                                           "#define COMMON_HPP\n"
                                           "#endif\n";
  const std::string_view a_hpp_code = "#include \"common.hpp\"\n"
                                      "#ifndef A_HPP\n"
                                      "#define A_HPP\n"
                                      "int bar() { return 42; }\n"
                                      "#endif\n";
  const std::string_view b_hpp_code = "#include \"common.hpp\"\n"
                                      "#ifndef B_HPP\n"
                                      "#define B_HPP\n"
                                      "int foo() { return 42; }\n"
                                      "#endif\n";
  const std::string_view main_cpp_code = "#include \"a.hpp\"\n"
                                         "#include \"b.hpp\"\n";
  const std::string_view main2_cpp_code = "#include \"b.hpp\"\n"
                                          "#include \"a.hpp\"\n";

  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path working_directory = root / "working_dir";
  fs->addFile((working_directory / "common.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(common_hpp_code));
  fs->addFile((working_directory / "a.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(a_hpp_code));
  fs->addFile((working_directory / "b.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(b_hpp_code));
  fs->addFile((working_directory / "main.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  fs->addFile((working_directory / "main2.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main2_cpp_code));

  Graph g;
  const Graph::vertex_descriptor common_hpp =
      add_vertex(file_node("common.hpp")
                     .with_cost(0, common_hpp_code.size() * B)
                     .set_internal_parents(2),
                 g);
  const Graph::vertex_descriptor a_hpp = add_vertex(
      file_node("a.hpp").with_cost(0, 0 * B).set_internal_parents(2), g);
  const Graph::vertex_descriptor b_hpp = add_vertex(
      file_node("b.hpp").with_cost(0, 0 * B).set_internal_parents(2), g);
  const Graph::vertex_descriptor main_cpp =
      add_vertex(file_node("main.cpp")
                     .with_cost(10, (a_hpp_code.size() + b_hpp_code.size() +
                                     main_cpp_code.size()) *
                                        B),
                 g);
  const Graph::vertex_descriptor main2_cpp =
      add_vertex(file_node("main2.cpp")
                     .with_cost(10, (a_hpp_code.size() + b_hpp_code.size() +
                                     main2_cpp_code.size()) *
                                        B),
                 g);

  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 1}, g);
  add_edge(main_cpp, b_hpp, {"\"b.hpp\"", 2}, g);
  add_edge(main2_cpp, b_hpp, {"\"b.hpp\"", 1}, g);
  add_edge(main2_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(a_hpp, common_hpp, {"\"common.hpp\"", 1}, g);
  add_edge(b_hpp, common_hpp, {"\"common.hpp\"", 1}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files,
              UnorderedElementsAre(
                  VertexDescriptorIs(results->graph,
                                     Field(&file_node::path, Eq("a.hpp"))),
                  VertexDescriptorIs(results->graph,
                                     Field(&file_node::path, Eq("b.hpp")))));
}

// Check external include guards are "working" as expected.
// Unfortunately that means that we hide some dependencies when
// using external includes for the moment.
TEST_P(BuildGraphTest, ExternalIncludeGuards) {
  const std::string_view common_hpp_code = "#ifndef COMMON_HPP\n"
                                           "#define COMMON_HPP\n"
                                           "#endif\n";
  const std::string_view a_hpp_code = "#pragma once\n"
                                      ""
                                      "#ifndef COMMON_HPP\n"
                                      "#include \"common.hpp\"\n"
                                      "#endif\n"
                                      "char bar() { return 'a'; }\n";
  const std::string_view b_hpp_code = "#pragma once\n"
                                      ""
                                      "#ifndef COMMON_HPP\n"
                                      "#include \"common.hpp\"\n"
                                      "#endif\n"
                                      "char bar() { return '\\0'; }\n";
  const std::string_view c_hpp_code = "#pragma once\n"
                                      "#include \"b.hpp\"\n";
  const std::string_view main_cpp_code = "#include \"a.hpp\"\n"
                                         "#include \"c.hpp\"\n";
  const std::string_view main2_cpp_code = "#include \"c.hpp\"\n"
                                          "#include \"a.hpp\"\n";

  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path working_directory = root / "working_dir";
  fs->addFile((working_directory / "common.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(common_hpp_code));
  fs->addFile((working_directory / "a.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(a_hpp_code));
  fs->addFile((working_directory / "b.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(b_hpp_code));
  fs->addFile((working_directory / "c.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(c_hpp_code));
  fs->addFile((working_directory / "main.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  fs->addFile((working_directory / "main2.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main2_cpp_code));

  Graph g;
  const Graph::vertex_descriptor common_hpp =
      add_vertex(file_node("common.hpp")
                     .with_cost(0, common_hpp_code.size() * B)
                     .set_internal_parents(1),
                 g);
  const Graph::vertex_descriptor a_hpp =
      add_vertex(file_node("a.hpp")
                     .with_cost(9, a_hpp_code.size() * B)
                     .set_internal_parents(2),
                 g);
  const Graph::vertex_descriptor b_hpp =
      add_vertex(file_node("b.hpp")
                     .with_cost(9, b_hpp_code.size() * B)
                     .set_internal_parents(1),
                 g);
  const Graph::vertex_descriptor c_hpp =
      add_vertex(file_node("c.hpp")
                     .with_cost(0, c_hpp_code.size() * B)
                     .set_internal_parents(2),
                 g);
  const Graph::vertex_descriptor main_cpp = add_vertex(
      file_node("main.cpp").with_cost(1, main_cpp_code.size() * B), g);
  const Graph::vertex_descriptor main2_cpp = add_vertex(
      file_node("main2.cpp").with_cost(1, main2_cpp_code.size() * B), g);

  add_edge(main_cpp, a_hpp, {"\"a.hpp\"", 1}, g);
  add_edge(main_cpp, c_hpp, {"\"c.hpp\"", 2}, g);
  add_edge(main2_cpp, c_hpp, {"\"c.hpp\"", 1}, g);
  add_edge(main2_cpp, a_hpp, {"\"a.hpp\"", 2}, g);
  add_edge(a_hpp, common_hpp, {"\"common.hpp\"", 3}, g);
  add_edge(c_hpp, b_hpp, {"\"b.hpp\"", 2}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

// This checks that we can refer to files through multiple different
// names without issues.  This covers issues found on Windows as it
// does not care about capitalization and occassionally Clang gives
// us forwardslashes instead of backslashes.  This caused a bug when
// `--smaller-file-opt=true` was enabled and there were multiple
// source files including a common header either with different
// capitalization, or including `#include <windows.h>` and
// `#include "windows.h"` (the latter would have a forward slash before
// the filename instead of a backslash).
TEST_P(BuildGraphTest, DifferentCapitalization) {
  const std::string_view windows_h_code = "#pragma once\n"
                                          "#define min(x,y) ((x<y)?x:y)\n";
  const std::string_view main_cpp_code = "#include \"windows.h__\"\n";
  const std::string_view main2_cpp_code = "#include \"windows.h___\"\n";

  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path working_directory = root / "working_dir";
  fs->addFile((working_directory / "windows.h").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(windows_h_code));
  fs->addFile((working_directory / "main.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main_cpp_code));
  fs->addFile((working_directory / "main2.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(main2_cpp_code));

  Graph g;
  const Graph::vertex_descriptor windows_h =
      add_vertex(file_node("windows.h__")
                     .with_cost(0, windows_h_code.size() * B)
                     .set_internal_parents(2),
                 g);
  const Graph::vertex_descriptor main_cpp = add_vertex(
      file_node("main.cpp").with_cost(1, main_cpp_code.size() * B), g);
  const Graph::vertex_descriptor main2_cpp = add_vertex(
      file_node("main2.cpp").with_cost(1, main2_cpp_code.size() * B), g);

  add_edge(main_cpp, windows_h, {"\"windows.h__\"", 1}, g);
  add_edge(main2_cpp, windows_h, {"\"windows.h___\"", 1}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, llvm::makeIntrusiveRefCnt<TrimFileSystem>(fs),
      get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, IsEmpty());
}

// Test that we can have a source file recursively include itself.
// This is sometimes seen in the wild in unit tests that want
// parameterized tests for testing macros.
TEST_P(BuildGraphTest, RecursiveSource) {
  const std::string_view a_cpp_code = "#ifndef COUNT\n"
                                      "  #define COUNT 0\n"
                                      "  #include \"a.cpp\"\n"
                                      "#elsif COUNT == 1\n"
                                      "  #undef COUNT\n"
                                      "  #define COUNT 2\n"
                                      "  #include \"a.cpp\"\n"
                                      "#elsif COUNT == 2\n"
                                      "  #undef COUNT\n"
                                      "  #define COUNT 3\n"
                                      "  #include \"a.cpp\"\n"
                                      "#endif\n";

  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path working_directory = root / "working_dir";
  fs->addFile((working_directory / "a.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(a_cpp_code));

  Graph g;
  const Graph::vertex_descriptor a_cpp =
      add_vertex(file_node("a.cpp")
                     .with_cost(1, 4 * a_cpp_code.size() * B)
                     .set_internal_parents(1),
                 g);

  add_edge(a_cpp, a_cpp, {"\"a.cpp\"", 3}, g);

  llvm::Expected<build_graph::result> results = build_graph::from_dir(
      working_directory, {}, fs, get_file_type, GetParam());
  EXPECT_THAT(results->graph, GraphsAreEquivalent(g));
  EXPECT_THAT(results->missing_includes, IsEmpty());
  EXPECT_THAT(results->unguarded_files, UnorderedElementsAre(a_cpp));
}

INSTANTIATE_TEST_SUITE_P(
    SmallFileOptimization, BuildGraphTest,
    Values(build_graph::options(),
           build_graph::options().enable_replace_file_optimization(true)));

} // namespace
