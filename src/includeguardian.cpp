#include "includeguardian.hpp"

#include "build_graph.hpp"
#include "dot_graph.hpp"
#include "find_expensive_files.hpp"
#include "find_expensive_headers.hpp"
#include "find_expensive_includes.hpp"
#include "find_unnecessary_sources.hpp"
#include "find_unused_components.hpp"
#include "get_total_cost.hpp"
#include "graph.hpp"
#include "list_included_files.hpp"
#include "recommend_precompiled.hpp"

#include <boost/units/io.hpp>

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <clang/Basic/SourceManager.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <utility>

namespace IncludeGuardian {

namespace {

struct file_node_printer {
  const file_node &node;
};

// TODO: Move this to a component and unit test it
std::string format_file_size(
    boost::units::quantity<boost::units::information::info> file_size) {
  // For some reason when we use `boost::units::binary_prefix` it converts
  // to bits, e.g. kib and Mib, instead of sticking with bytes.  Here we
  // can make sure we stick with bytes and can also print out to 3 signficant
  // figures instead of using decimal places.
  using namespace boost::units::information;

  assert(file_size >= 0 * bytes);

  std::ostringstream ss;

  // Technically our files size could be larger than all of these and
  // we overflow this array, but it's not really realistic :)
  const std::string_view suffixes[] = {"B",   "KiB", "MiB", "GiB",
                                       "TiB", "PiB", "EiB", "ZiB"};
  auto suffix = suffixes;
  while (file_size >= 1024 * bytes) {
    file_size /= 1024.0;
    ++suffix;
  }

  // Try to display to 3 significant figures, but print out 4 if we have
  // something like "1023 KiB" as "1020 KiB" would be inaccurate and still take
  // the same amount of space
  if (file_size >= 1000 * bytes) {
    ss << static_cast<int>(std::round(file_size.value())) << ' ' << *suffix;
  } else {
    const int sf = 3;
    const int rounded = std::round(file_size.value()) * std::pow(10, sf);
    const int precision = rounded < 10000 ? 2 : (rounded < 100000 ? 1 : 0);
    ss << std::setprecision(precision) << std::fixed;
    ss << (rounded / std::pow(10, sf)) << ' ' << *suffix;
  }

  return ss.str();
}

file_node_printer pretty_path(const file_node &n) {
  return file_node_printer{n};
}

std::ostream &operator<<(std::ostream &stream, file_node_printer v) {
  if (v.node.is_external) {
    return stream << '<' << v.node.path.string() << '>';
  } else {
    return stream << '"' << v.node.path.string() << '"';
  }
}

const std::pair<std::string_view, build_graph::file_type> lookup[] = {
    {"cpp", build_graph::file_type::source},
    {"c", build_graph::file_type::source},
    {"cc", build_graph::file_type::source},
    {"C", build_graph::file_type::source},
    {"cxx", build_graph::file_type::source},
    {"c++", build_graph::file_type::source},
    {"hpp", build_graph::file_type::header},
    {"h", build_graph::file_type::header},
    {"hh", build_graph::file_type::header},
    {"H", build_graph::file_type::header},
    {"hxx", build_graph::file_type::header},
    {"h++", build_graph::file_type::header},
    {"", build_graph::file_type::ignore},
};
build_graph::file_type map_ext(std::string_view file) {
  // CMake generates precompiled headers with this name
  if (file.ends_with("cmake_pch.hxx")) {
    return build_graph::file_type::precompiled_header;
  }
  const auto dot = std::find(file.rbegin(), file.rend(), '.').base();
  const std::string_view ext(dot, file.end());
  // Use end-1 because if we fail to find then the true last element is 'ignore'
  return std::find_if(std::begin(lookup), std::end(lookup) - 1,
                      [=](auto p) { return p.first == ext; })
      ->second;
}

get_total_cost::result get_naive_cost(const Graph &g) {
  const auto [begin, end] = vertices(g);
  return std::accumulate(
      begin, end, get_total_cost::result{},
      [&](get_total_cost::result acc, const Graph::vertex_descriptor v) {
        get_total_cost::result r;
        if (g[v].is_precompiled) {
          r.precompiled += g[v].underlying_cost;
        } else {
          r.true_cost += g[v].underlying_cost;
        }
        return acc + r;
      });
}

std::vector<std::pair<std::filesystem::path, clang::SrcMgr::CharacteristicKind>>
parse_include_dirs(const llvm::cl::list<std::string> &include_dirs,
                   const llvm::cl::list<std::string> &system_include_dirs) {
  std::vector<std::tuple<std::filesystem::path,
                         clang::SrcMgr::CharacteristicKind, unsigned>>
      tmp(include_dirs.size() + system_include_dirs.size());

  auto out = tmp.begin();

  for (auto it = include_dirs.begin(); it != include_dirs.end(); ++it, ++out) {
    *out = std::make_tuple(std::filesystem::path(*it), clang::SrcMgr::C_User,
                           include_dirs.getPosition(it - include_dirs.begin()));
  }

  for (auto it = system_include_dirs.begin(); it != system_include_dirs.end();
       ++it, ++out) {
    *out = std::make_tuple(
        std::filesystem::path(*it), clang::SrcMgr::C_System,
        system_include_dirs.getPosition(it - system_include_dirs.begin()));
  }

  std::inplace_merge(tmp.begin(), tmp.begin() + include_dirs.size(), tmp.end(),
                     [](const auto &l, const auto &r) {
                       return std::get<unsigned>(l) < std::get<unsigned>(r);
                     });

  std::vector<
      std::pair<std::filesystem::path, clang::SrcMgr::CharacteristicKind>>
      result;
  std::transform(
      tmp.begin(), tmp.end(), std::back_inserter(result), [](const auto &t) {
        return std::make_pair(std::get<std::filesystem::path>(t),
                              std::get<clang::SrcMgr::CharacteristicKind>(t));
      });
  return result;
}

class stopwatch {
  std::chrono::steady_clock::time_point m_start;

public:
  stopwatch() : m_start(std::chrono::steady_clock::now()) {}

  std::chrono::steady_clock::duration restart() {
    auto const now = std::chrono::steady_clock::now();
    return now - std::exchange(m_start, now);
  }
};

enum class output {
  dot_graph,
  list_files,
  most_expensive,
};

} // namespace

int run(int argc, const char **argv, std::ostream &out, std::ostream &err) {
  llvm::cl::opt<output> output(
      "output", llvm::cl::desc("Choose the output"),
      llvm::cl::values(
          llvm::cl::OptionEnumValue(
              "dot-graph", static_cast<int>(output::dot_graph), "DOT graph"),
          llvm::cl::OptionEnumValue(
              "list-files", static_cast<int>(output::list_files), "List files"),
          llvm::cl::OptionEnumValue(
              "most-expensive", static_cast<int>(output::most_expensive),
              "List of most expensive include directives (default)")),
      llvm::cl::init(output::most_expensive));

  llvm::cl::opt<std::string> dir("dir", llvm::cl::desc("Choose the directory"),
                                 llvm::cl::value_desc("directory"),
                                 llvm::cl::Required);

  llvm::cl::list<std::string> include_dirs(
      "I", llvm::cl::desc("Additional include directories"),
      llvm::cl::ZeroOrMore);

  llvm::cl::list<std::string> system_include_dirs(
      "isystem", llvm::cl::desc("Additional system include directories"),
      llvm::cl::ZeroOrMore);

  llvm::cl::list<std::string> forced_includes(
      "forced-includes",
      llvm::cl::desc("Forced includes (absolute path preferred)"),
      llvm::cl::ZeroOrMore);

  llvm::cl::opt<double> cutoff(
      "cutoff", llvm::cl::desc("Cutoff percentage for suggestions"),
      llvm::cl::value_desc("percentage"), llvm::cl::init(1.0));
  llvm::cl::opt<double> pch_ratio(
      "pch-ratio",
      llvm::cl::desc(
          "Require ratio of token reduction compared to pch file growth"),
      llvm::cl::value_desc("ratio"), llvm::cl::init(2.0));
  llvm::cl::opt<bool> smaller_file_opt(
      "smaller-file-opt",
      llvm::cl::desc(
          "Whether to enable an optimization to improve preprocessing time by "
          "replacing already seen files with a smaller version for further "
          "sources "),
      llvm::cl::value_desc("enabled"), llvm::cl::init(false));

  if (!llvm::cl::ParseCommandLineOptions(argc, argv)) {
    return 1;
  }

  if (cutoff.getValue() < 0.0 || cutoff.getValue() > 100.0) {
    err << "'cutoff' must lie between [0, 100]\n";
    return 1;
  }

  const double percent_cut_off = cutoff.getValue() / 100.0;

  if (pch_ratio.getValue() <= 0.0) {
    err << "'pch-ratio' must be positive\n";
    return 1;
  }

  stopwatch timer;

  std::vector<std::filesystem::path> forced_includes_files(
      forced_includes.size());
  std::transform(forced_includes.begin(), forced_includes.end(),
                 forced_includes_files.begin(),
                 [](const std::string &s) { return std::filesystem::path(s); });

  llvm::Expected<build_graph::result> result = build_graph::from_dir(
      dir.getValue(), parse_include_dirs(include_dirs, system_include_dirs),
      llvm::vfs::getRealFileSystem(), map_ext,
      build_graph::options().enable_replace_file_optimization(smaller_file_opt),
      forced_includes_files);

  if (!result) {
    // TODO: error message
    err << "Error";
    return 1;
  }

  out << "Summary\n";
  out << "=======\n";
  out << "Graph built in "
      << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";

  const auto &graph = result->graph;
  const auto &sources = result->sources;
  const auto &missing = result->missing_includes;
  const auto &unguarded = result->unguarded_files;
  out << "Found " << sources.size() << " sources, " << num_vertices(graph)
      << " files total, and " << num_edges(graph)
      << " #include directives.\n\n";

  const get_total_cost::result naive_cost = get_naive_cost(graph);
  const get_total_cost::result project_cost =
      get_total_cost::from_graph(graph, sources);

  out << "Overview\n";
  out << "========\n";
  out << "Total file size = " << format_file_size(naive_cost.total().file_size)
      << '\n';
  out << "Token count = " << naive_cost.total().token_count << '\n';
  out << "Total translation unit file size = "
      << format_file_size(project_cost.total().file_size) << '\n';
  out << "Translation Unit Token count = " << project_cost.total().token_count
      << '\n';
  if (naive_cost.precompiled.token_count > 0u) {
    out << "Precompiled header (PCH) file size = "
        << format_file_size(naive_cost.precompiled.file_size) << '\n';
    out << "Precompiled header (PCH) token count = "
        << naive_cost.precompiled.token_count << '\n';
    out << "Total translation unit file size without PCH = "
        << format_file_size(project_cost.true_cost.file_size) << " ("
        << (100.0 * project_cost.true_cost.file_size /
            project_cost.total().file_size)
               .value()
        << "%)\n";
    out << "Total translation unit token count without PCH = "
        << project_cost.true_cost.token_count << " ("
        << (100.0 * project_cost.true_cost.token_count /
            project_cost.total().token_count)
        << "%)\n";
  }

  timer.restart();

  out << "\n";
  out << "Source files\n";
  out << "============\n";
  for (const Graph::vertex_descriptor v : sources) {
    out << "  - " << pretty_path(graph[v]) << '\n';
  }
  out << '\n';

  if (!missing.empty()) {
    out << "Missing files\n";
    out << "==============\n";
    out << "There are " << missing.size() << " missing files\n";
    std::copy(missing.begin(), missing.end(),
              std::ostream_iterator<std::string>(out, "  \n  "));
  }
  out << '\n';

  out << "Recommendations\n";
  out << "===============\n";
  out << "There are " << unguarded.size()
      << " files that do not have an include guard that is strict enough "
         "to trigger the multiple-inclusion optimization where compilers "
         "will skip opening a file a second time for each translation "
         "unit\n";
  if (!unguarded.empty()) {
    std::vector<Graph::vertex_descriptor> unguarded_copy;
    std::copy_if(unguarded.begin(), unguarded.end(),
                 std::back_inserter(unguarded_copy),
                 [&](const Graph::vertex_descriptor v) {
                   return in_degree(v, graph) > 1;
                 });
    std::sort(unguarded_copy.begin(), unguarded_copy.end(),
              [&](Graph::vertex_descriptor l, Graph::vertex_descriptor r) {
                return in_degree(l, graph) > in_degree(r, graph);
              });
    std::vector<std::string> files(unguarded_copy.size());
    std::transform(unguarded_copy.begin(), unguarded_copy.end(), files.begin(),
                   [&](Graph::vertex_descriptor v) {
                     std::ostringstream out;
                     out << "  - " << pretty_path(graph[v]) << " included by "
                         << in_degree(v, graph) << " files\n";
                     return out.view();
                   });
    std::copy(files.begin(), files.end(),
              std::ostream_iterator<std::string>(out));
  }

  switch (output) {
  case output::dot_graph: {
    dot_graph::print(graph, out);
    out << "Graph printed in "
        << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";
    return 0;
  }
  case output::list_files: {
    {
      std::vector<list_included_files::result> results =
          list_included_files::from_graph(graph, sources);
      out << "Files found in "
          << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";
      std::sort(results.begin(), results.end(),
                [&](const list_included_files::result &l,
                    const list_included_files::result &r) {
                  return l.source_that_can_reach_it_count *
                             graph[l.v].true_cost().token_count >
                         r.source_that_can_reach_it_count *
                             graph[r.v].true_cost().token_count;
                });
      for (const list_included_files::result &i : results) {
        const cost c =
            i.source_that_can_reach_it_count * graph[i.v].true_cost();
        const double percentage =
            (100.0 * c.token_count) / project_cost.true_cost.token_count;
        out << std::setprecision(2) << std::fixed << c.token_count << " ("
            << percentage << "%) " << graph[i.v].path.filename().string()
            << " x" << i.source_that_can_reach_it_count << "\n";
      }
    }
    return 0;
  }
  case output::most_expensive: {
    {
      std::vector<component_and_cost> results =
          find_unused_components::from_graph(graph, sources, 0u);
      out << "\nThis is a list of all source files that should be "
             "considered for removal as no other files include "
             "their header file. This analysis took "
          << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";
      std::sort(results.begin(), results.end(),
                [](const component_and_cost &l, const component_and_cost &r) {
                  return l.saving.token_count > r.saving.token_count;
                });
      for (const component_and_cost &i : results) {
        const double percentage =
            (100.0 * i.saving.token_count) / project_cost.true_cost.token_count;
        out << "  - " << std::setprecision(2) << std::fixed
            << i.saving.token_count << " (" << percentage << "%) removing "
            << pretty_path(*i.source) << '\n';
      }
    }
    {
      std::vector<include_directive_and_cost> results =
          find_expensive_includes::from_graph(
              graph, sources,
              project_cost.true_cost.token_count * percent_cut_off);
      out << "\nThis is a list of all #include directives that should be "
             "considered for removal, ordered by benefit. "
             "This analysis took "
          << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";
      std::sort(results.begin(), results.end(),
                [](const include_directive_and_cost &l,
                   const include_directive_and_cost &r) {
                  return l.saving.token_count > r.saving.token_count;
                });
      for (const include_directive_and_cost &i : results) {
        const double percentage =
            (100.0 * i.saving.token_count) / project_cost.true_cost.token_count;
        out << "  - " << std::setprecision(2) << std::fixed
            << i.saving.token_count << " (" << percentage
            << "%) remove #include " << i.include->code << " from "
            << i.file.filename().string() << "L#" << i.include->lineNumber
            << "\n";
      }
    }

    {
      std::vector<find_expensive_headers::result> results =
          find_expensive_headers::from_graph(
              graph, sources,
              project_cost.true_cost.token_count * percent_cut_off);
      out << "\nThis is a list of all header files that should be considered "
             "to move from a components header to the source file, ordered by "
             "by benefit. This analysis took "
          << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";
      std::sort(results.begin(), results.end(),
                [](const find_expensive_headers::result &l,
                   const find_expensive_headers::result &r) {
                  return l.total_saving().token_count >
                         r.total_saving().token_count;
                });
      for (const find_expensive_headers::result &i : results) {
        const double percentage = (100.0 * i.total_saving().token_count) /
                                  project_cost.true_cost.token_count;
        out << "  - " << std::setprecision(2) << std::fixed
            << i.total_saving().token_count << " (" << percentage
            << "%) moving " << graph[i.v].internal_incoming << " references to "
            << graph[i.v].path.string() << "\n";
      }
    }

    {
      std::vector<recommend_precompiled::result> results =
          recommend_precompiled::from_graph(graph, sources,
                                            project_cost.true_cost.token_count *
                                                percent_cut_off,
                                            pch_ratio.getValue());
      out << "\nThis is a list of all header files that should be considered "
             "to be added to the precompiled header, ordered by "
             "by benefit. This analysis took "
          << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";
      std::sort(results.begin(), results.end(),
                [](const recommend_precompiled::result &l,
                   const recommend_precompiled::result &r) {
                  return l.saving.token_count > r.saving.token_count;
                });
      for (const recommend_precompiled::result &i : results) {
        const double percentage =
            (100.0 * i.saving.token_count) / project_cost.true_cost.token_count;
        out << std::setprecision(2) << std::fixed << i.saving.token_count
            << "  - (" << percentage << "%) adding " << graph[i.v].path.string()
            << " to a precompiled header\n";
      }
    }

    {
      // Assume that each "expensive" file could be reduced this much
      const double assumed_reduction = 0.50;
      std::vector<file_and_cost> results = find_expensive_files::from_graph(
          graph, sources,
          project_cost.true_cost.token_count * percent_cut_off /
              assumed_reduction);
      out << "\nThis is a list of all files that should be considered "
             "to be simplified or split into smaller parts and #includes "
             "updated, ordered by by benefit. This analysis took "
          << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";
      std::sort(
          results.begin(), results.end(),
          [](const file_and_cost &l, const file_and_cost &r) {
            return static_cast<std::size_t>(l.node->true_cost().token_count) *
                       l.sources >
                   static_cast<std::size_t>(r.node->true_cost().token_count) *
                       r.sources;
          });
      for (const file_and_cost &i : results) {
        const unsigned saving =
            i.sources * assumed_reduction * i.node->true_cost().token_count;
        const double percentage =
            (100.0 * saving) / project_cost.true_cost.token_count;
        out << "  - " << std::setprecision(2) << std::fixed << saving << " ("
            << percentage << "%) from "
            << std::filesystem::path(i.node->path).filename().string()
            << " by simplifing or splitting by " << 100 * assumed_reduction
            << "%\n";
      }
    }

    {
      std::vector<find_unnecessary_sources::result> results =
          find_unnecessary_sources::from_graph(
              graph, sources,
              project_cost.true_cost.token_count * percent_cut_off);
      out << "\nThis is a list of all source files that should be considered "
             "to be inlined into the header and removed as a translation unit, "
             "ordered by by benefit.\nThis analysis took "
          << duration_cast<std::chrono::milliseconds>(timer.restart()) << "\n";
      std::sort(results.begin(), results.end(),
                [](const find_unnecessary_sources::result &l,
                   const find_unnecessary_sources::result &r) {
                  return l.total_saving().token_count >
                         r.total_saving().token_count;
                });
      for (const find_unnecessary_sources::result &i : results) {
        const double percentage = (100.0 * i.total_saving().token_count) /
                                  project_cost.true_cost.token_count;
        out << "  - " << std::setprecision(2) << std::fixed
            << i.total_saving().token_count << " (" << percentage
            << "%) deleting " << graph[i.source].path
            << " and putting its contents in "
            << graph[*graph[i.source].component].path << "\n";
      }
    }
    return 0;
  }
  default:
    err << "Unknown output argument";
    return 1;
  }
}

} // namespace IncludeGuardian