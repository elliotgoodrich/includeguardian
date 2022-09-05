#include <benchmark/benchmark.h>

#include <llvm/Support/VirtualFileSystem.h>

#include "build_graph.hpp"

#include <random>

namespace {

using namespace IncludeGuardian;

const std::pair<std::string_view, build_graph::file_type> lookup[] = {
    {"cpp", build_graph::file_type::source},
    {"hpp", build_graph::file_type::header},
    {"", build_graph::file_type::ignore},
};

build_graph::file_type map_ext(std::string_view file) {
  const auto dot = std::find(file.rbegin(), file.rend(), '.').base();
  const std::string_view ext(dot, file.end());
  // Use end-1 because if we fail to find then the true last element is 'ignore'
  return std::find_if(std::begin(lookup), std::end(lookup) - 1,
                      [=](auto p) { return p.first == ext; })
      ->second;
}

std::string int_to_file(int n) { return std::to_string(n) + ".hpp"; }

// Create an in-memory file system that would create the specified `graph`.
std::tuple<std::vector<std::filesystem::path>,
           llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem>,
           boost::units::quantity<boost::units::information::info>>
make_file_system(const std::filesystem::path &working_directory,
                 int source_count, double rough_probability_to_include) {
  auto fs = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  std::vector<std::filesystem::path> sources;

  std::mt19937 gen;

  auto total_size = 0.0 * boost::units::information::bytes;

  std::string file_contents;
  for (int i = 0; i < source_count; ++i) {
    for (bool is_source : {true, false}) {
      file_contents = "#pragma once\n";
      // Include the source header
      if (is_source) {
        file_contents += "#include ";
        file_contents += '\"';
        file_contents += std::to_string(i);
        file_contents += ".hpp\"\n";
      }

      std::vector<int> includes(i);
      std::iota(includes.begin(), includes.end(), 0);
      std::shuffle(includes.begin(), includes.end(), gen);

      if (i != 0) {
        std::poisson_distribution d(i * rough_probability_to_include);

        const auto count = d(gen);
        const auto end =
            includes.begin() + std::min<std::size_t>(count, includes.size());
        for (auto it = includes.begin(); it != end; ++it) {
          file_contents += "#include ";
          file_contents += '\"';
          file_contents += std::to_string(*it);
          file_contents += ".hpp\"\n";
        }
      }

      file_contents += "#pragma override_file_size(1000)\n";
      file_contents += "#pragma override_token_count(100)\n";

      const char *extension = is_source ? ".cpp" : ".hpp";
      const std::filesystem::path p =
          working_directory / (int_to_file(i) + extension);
      fs->addFile(p.string(), 0,
                  llvm::MemoryBuffer::getMemBufferCopy(file_contents));
      total_size += file_contents.size() * boost::units::information::bytes;
      if (is_source) {
        sources.push_back(p);
      }
    }
  }
  return {sources, fs, total_size};
}

static void BM_SomeFunction(benchmark::State &state) {
  const std::filesystem::path root = "C:\\";
  auto [sources, fs, total_size] = make_file_system(root, 1000, 0.1);

  for (auto _ : state) {
    [[maybe_unused]] auto r = build_graph::from_dir(root, {}, fs, &map_ext);
  }

  state.SetItemsProcessed(sources.size());
  state.SetBytesProcessed(total_size.value());
}

} // namespace

// Register the function as a benchmark
BENCHMARK(BM_SomeFunction);

// Run the benchmark
BENCHMARK_MAIN();
