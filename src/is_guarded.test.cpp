#include "build_graph.hpp"

#include "matchers.hpp"

#include <llvm/Support/VirtualFileSystem.h>
#include <clang/Tooling/CompilationDatabase.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <initializer_list>
#include <ostream>
#include <string_view>

using namespace IncludeGuardian;

namespace {

using namespace testing;

const auto B = boost::units::information::byte;

class TestCompilationDatabase : public clang::tooling::CompilationDatabase {
  std::filesystem::path m_working_dir;
public:
  TestCompilationDatabase(const std::filesystem::path& working_dir)
  : m_working_dir(working_dir) {
  }

  std::vector<clang::tooling::CompileCommand>
  getCompileCommands(clang::StringRef FilePath) const final {
    // Note for the preprocessor we do not need `-o` flags and it is
    // specifically stripped out by an `ArgumentAdjuster`.
    using namespace std::string_literals;

    std::vector<std::string> things;
    things.emplace_back("/usr/bin/clang++");
    things.emplace_back(FilePath.str());
    things.emplace_back("-DALREADY_GUARDED");
    return {{m_working_dir.string(), FilePath, std::move(things), "out"}};
  }

  /// Returns the list of all files available in the compilation database.
  ///
  /// By default, returns nothing. Implementations should override this if they
  /// can enumerate their source files.
  std::vector<std::string> getAllFiles() const final {
    return { (m_working_dir / "main.cpp").string() };
  }
};


class IsGuardedTest
    : public testing::TestWithParam<
          std::tuple<std::pair<const char *, bool>, build_graph::options>> {};

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

TEST_P(IsGuardedTest, MainTest) {
  const auto [input_output, options] = GetParam();
  const auto [header, is_guarded] = input_output;
  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  const std::filesystem::path working_directory = root / "working_dir";
  fs->addFile((working_directory / "header.hpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(header));
  fs->addFile((working_directory / "main.cpp").string(), 0,
              llvm::MemoryBuffer::getMemBufferCopy(R"foo(
#include "header.hpp"
#include "header.hpp"
#include "header.hpp"
#include "header.hpp"
#include "header.hpp"

int main() {}
)foo"));

  TestCompilationDatabase db(working_directory);

  const build_graph::result results =
      *build_graph::from_compilation_db(db, working_directory, { working_directory / "main.cpp" }, get_file_type, fs, options);

  EXPECT_THAT(results.missing_includes, IsEmpty());
  EXPECT_THAT(results.unguarded_files, SizeIs(is_guarded ? 0 : 1));
}

INSTANTIATE_TEST_SUITE_P(
    SmallFileOptimization, IsGuardedTest,
    Combine(
        Values(std::make_pair(R"code(
#pragma once)code",
                              true),
               std::make_pair(R"code(
#ifndef INCLUDE_GUARD
#define INCLUDE_GUARD
#endif)code",
                              true),
               std::make_pair(R"code(
 // comment
#ifndef INCLUDE_GUARD
#define INCLUDE_GUARD
#endif
/* comment */
)code",
                              true),
               std::make_pair(R"code(
# // null directive
#ifndef INCLUDE_GUARD
#define INCLUDE_GUARD
#endif
# // null directive
)code",
                              false),
               std::make_pair(R"code(
#ifdef SECOND
  #pragma once
#else
  #define SECOND
#endif)code",
                              true),
               std::make_pair(R"code(
_Pragma("once"))code",
                              true),
               std::make_pair(R"code(
#define ONCE "once"
_Pragma(ONCE))code",
                              true),
               std::make_pair(R"code(
#ifndef INCLUDE_GUARD
#ifndef INCLUDE_GUARD_TWICE
#define INCLUDE_GUARD
#endif

#define INCLUDE_GUARD_TWICE
#endif
)code",
                              true),
               std::make_pair(R"code(
#ifndef ALREADY_GUARDED

#endif
)code",
                              false),
               std::make_pair(R"code(
#if !defined(INCLUDE_GUARD)
#ifndef FOO
#endif
#ifdef FOO
#endif
#define INCLUDE_GUARD
#endif
)code",
                              false),
               std::make_pair(R"code()code", false)),
        Values(build_graph::options(),
               build_graph::options().enable_replace_file_optimization(true))));

} // namespace
