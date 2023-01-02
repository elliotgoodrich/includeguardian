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

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/graph/adj_list_serialize.hpp>
#include <boost/units/io.hpp>

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CommonOptionsParser.h>

#include <chrono>
#include <fstream>
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

template <typename TIME> std::string format_time(TIME t) {
  std::ostringstream ss;
  const double ms = duration_cast<std::chrono::milliseconds>(t).count();
  const double s = std::round(ms) / 1000;
  ss << s;
  return ss.str();
}

// Format `s` so that it is printable as a YAML string
std::string format_str(std::string_view s) {
  if (std::any_of(s.begin(), s.end(),
                  [](char c) { return c == '\\' || c == '"'; })) {
    // If we contain a backslash or a double quote, then we need to
    // surround with single quotes and double up any single quotes.
    // This does not support non-printable characters, which need
    // to be surrounded by double quotes.
    std::string out;
    out += '\'';
    auto it = s.begin();
    while (true) {
      auto next = std::find(it, s.end(), '\'');
      out.append(it, next);
      if (next == s.end()) {
        break;
      }
      out += "''";
      it = next;
    }
    out += '\'';
    return out;
  } else {
    return std::string(s);
  }
}

file_node_printer pretty_path(const file_node &n) {
  return file_node_printer{n};
}

std::ostream &operator<<(std::ostream &stream, file_node_printer v) {
  std::ostringstream ss;
  if (v.node.is_external) {
    ss << '<' << v.node.path.string() << '>';
  } else {
    ss << '"' << v.node.path.string() << '"';
  }
  return stream << format_str(ss.view());
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

std::vector<std::string>
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

  std::vector<std::string> result;
  std::transform(
      tmp.begin(), tmp.end(), std::back_inserter(result), [](const auto &t) {
        return (isSystem(std::get<clang::SrcMgr::CharacteristicKind>(t))
                    ? "-isystem"
                    : "-I") +
               std::get<std::filesystem::path>(t).string();
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

class ReplacementCompilationDatabase
    : public clang::tooling::CompilationDatabase {
public:
  std::filesystem::path m_working_directory;
  std::vector<std::filesystem::path> m_sources;

  ReplacementCompilationDatabase(const std::filesystem::path &working_directory,
                                 llvm::vfs::FileSystem &fs)
      : m_working_directory(working_directory), m_sources() {
    std::vector<std::string> directories;
    directories.push_back(working_directory.string());
    const llvm::vfs::directory_iterator end;
    while (!directories.empty()) {
      const std::string dir_copy = std::move(directories.back());
      directories.pop_back();
      std::error_code ec;
      llvm::vfs::directory_iterator it = fs.dir_begin(dir_copy, ec);
      while (!ec && it != end) {
        if (it->type() == llvm::sys::fs::file_type::directory_file) {
          directories.push_back(it->path().str());
        } else if (it->type() == llvm::sys::fs::file_type::regular_file &&
                   map_ext(it->path()) == build_graph::file_type::source) {
          m_sources.emplace_back(it->path().str());
        }
        it.increment(ec);
      }
    }
  }

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
    // Note for the preprocessor we do not need `-o` flags and it is
    // specifically stripped out by an `ArgumentAdjuster`.
    using namespace std::string_literals;

    std::vector<std::string> things;
    things.emplace_back("/usr/bin/clang++");
    things.emplace_back(FilePath.str());
    return {{m_working_directory.string(), FilePath, std::move(things), "out"}};
  }

  /// Returns the list of all files available in the compilation database.
  ///
  /// By default, returns nothing. Implementations should override this if they
  /// can enumerate their source files.
  std::vector<std::string> getAllFiles() const final {
    std::vector<std::string> result(m_sources.size());
    std::transform(m_sources.cbegin(), m_sources.cend(), result.begin(),
                   [](const std::filesystem::path &p) { return p.string(); });
    return result;
  }
};

} // namespace

int run(int argc, const char **argv, std::ostream &out, std::ostream &err) {
  llvm::cl::OptionCategory build_category("Build Options");

  llvm::cl::opt<std::string> load_path("load", llvm::cl::desc("Load path"),
                                       llvm::cl::Optional,
                                       llvm::cl::cat(build_category));

  llvm::cl::opt<std::string> build_path("p", llvm::cl::desc("Build path"),
                                        llvm::cl::Optional,
                                        llvm::cl::cat(build_category));
  llvm::cl::opt<std::string> save_path("save", llvm::cl::desc("Save path"),
                                       llvm::cl::Optional,
                                       llvm::cl::cat(build_category));

  llvm::cl::list<std::string> source_paths(
      llvm::cl::Positional, llvm::cl::desc("<source0> [... <sourceN>]"),
      llvm::cl::ZeroOrMore, llvm::cl::cat(build_category));

  llvm::cl::opt<std::string> fake_compilation_db(
      "dir",
      llvm::cl::desc("Instead of looking for a compilation database "
                     "(compile_commands.json) use all C/C++ source files in "
                     "this directory"),
      llvm::cl::value_desc("directory"), llvm::cl::Optional,
      llvm::cl::cat(build_category));

  llvm::cl::list<std::string> include_dirs(
      "I", llvm::cl::desc("Additional include directories"),
      llvm::cl::ZeroOrMore, llvm::cl::cat(build_category));

  llvm::cl::list<std::string> system_include_dirs(
      "isystem", llvm::cl::desc("Additional system include directories"),
      llvm::cl::ZeroOrMore, llvm::cl::cat(build_category));

  llvm::cl::list<std::string> forced_includes(
      "forced-includes",
      llvm::cl::desc("Forced includes (absolute path preferred)"),
      llvm::cl::ZeroOrMore, llvm::cl::cat(build_category));

  llvm::cl::list<std::string> args_after(
      "extra-arg",
      llvm::cl::desc(
          "Additional argument to append to the compiler command line"),
      llvm::cl::cat(build_category));

  llvm::cl::list<std::string> args_before(
      "extra-arg-before",
      llvm::cl::desc(
          "Additional argument to prepend to the compiler command line"),
      llvm::cl::cat(build_category));

  llvm::cl::opt<bool> smaller_file_opt(
      "smaller-file-opt",
      llvm::cl::desc(
          "Whether to enable an optimization to improve preprocessing time by "
          "replacing already seen files with a smaller version for further "
          "sources "),
      llvm::cl::value_desc("enabled"), llvm::cl::init(true), llvm::cl::Hidden,
      llvm::cl::cat(build_category));

  llvm::cl::opt<bool> show_sources(
      "show-sources", llvm::cl::desc("Whether to output all source files"),
      llvm::cl::value_desc("enabled"), llvm::cl::init(false),
      llvm::cl::cat(build_category));

  llvm::cl::OptionCategory analysis_category("Analysis Options");

  llvm::cl::opt<bool> analyze(
      "analyze", llvm::cl::desc("Whether to perform analysis"),
      llvm::cl::value_desc("enabled"), llvm::cl::init(true),
      llvm::cl::cat(analysis_category));
  llvm::cl::opt<double> cutoff(
      "cutoff", llvm::cl::desc("Cutoff percentage for suggestions"),
      llvm::cl::value_desc("percentage"), llvm::cl::init(1.0),
      llvm::cl::cat(analysis_category));
  llvm::cl::opt<double> pch_ratio(
      "pch-ratio",
      llvm::cl::desc(
          "Require ratio of token reduction compared to pch file growth"),
      llvm::cl::value_desc("ratio"), llvm::cl::init(2.0),
      llvm::cl::cat(analysis_category));

  std::string ErrorMessage;
  std::unique_ptr<clang::tooling::FixedCompilationDatabase> foo =
      clang::tooling::FixedCompilationDatabase::loadFromCommandLine(
          argc, argv, ErrorMessage);
  if (!ErrorMessage.empty()) {
    ErrorMessage.append("\n");
  }
  const char *Overview = "";
  llvm::raw_string_ostream OS(ErrorMessage);
  // Stop initializing if command-line option parsing failed.
  if (!llvm::cl::ParseCommandLineOptions(argc, argv, Overview, &OS)) {
    OS.flush();
    return 1;
  }

  llvm::cl::PrintOptionValues();

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

  out << "---\n"
         "# Visit http://includeguarian.io/download for updates and\n"
         "# http://includeguarian.io/ci to keep your project building fast!\n"
         "stats:\n";
  out << "  version: " << format_str("0.0.2")
      << "\n"
         "  command: ";
  {
    std::string command = "includeguardian.exe ";
    for (int i = 0; i < argc; ++i) {
      command += ' ';
      command += argv[i];
    }
    out << format_str(command);
  }
  out << "\n  processing time: ";

  auto result = [&]() -> llvm::Expected<build_graph::result> {
    if (!load_path.empty()) {
      build_graph::result r;
      std::ifstream ifs(load_path.getValue()); // save to file
      boost::archive::text_iarchive ia(ifs);
      ia >> r.graph;
      out << format_time(timer.restart())
          << " # seconds to load serialized graph\n";
      return r;
    } else {
      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs =
          llvm::vfs::getRealFileSystem();

      std::unique_ptr<clang::tooling::CompilationDatabase> db;
      if (!build_path.empty()) {
        db = clang::tooling::CompilationDatabase::autoDetectFromDirectory(
            build_path, ErrorMessage);
      } else if (fake_compilation_db != "") {
        db = std::make_unique<ReplacementCompilationDatabase>(
            std::filesystem::path(fake_compilation_db.getValue()), *fs);
      } else if (!source_paths.empty()) {
        db = clang::tooling::CompilationDatabase::autoDetectFromSource(
            source_paths.front(), ErrorMessage);
      }

      if (!db) {
        llvm::errs() << "Error while trying to load a compilation database:\n"
                     << ErrorMessage << "Running without flags.\n";
        db.reset(new clang::tooling::FixedCompilationDatabase(".", {}));
      }

      // Add our adjustments to our database
      db = [&] {
        auto adjusting_db =
            std::make_unique<clang::tooling::ArgumentsAdjustingCompilations>(
                std::move(db));

        std::vector<std::string> forced_include_cmd(forced_includes.size());
        std::transform(forced_includes.begin(), forced_includes.end(),
                       forced_include_cmd.begin(), [](const std::string &file) {
                         return "-include " + file;
                       });

        // Add our forced-includes
        clang::tooling::ArgumentsAdjuster adjuster =
            clang::tooling::getInsertArgumentAdjuster(
                forced_include_cmd,
                clang::tooling::ArgumentInsertPosition::BEGIN);

        // Put everything at the beginning as either the position doesn't
        // matter, or the user wants to override something (e.g. put `-isystem`
        // instead of
        // `-I`) so we should put this first
        adjuster = clang::tooling::combineAdjusters(
            std::move(adjuster),
            clang::tooling::getInsertArgumentAdjuster(
                parse_include_dirs(include_dirs, system_include_dirs),
                clang::tooling::ArgumentInsertPosition::BEGIN));

        // Add the adjusters from the command line
        adjuster = clang::tooling::combineAdjusters(
            std::move(adjuster),
            clang::tooling::getInsertArgumentAdjuster(
                args_before, clang::tooling::ArgumentInsertPosition::BEGIN));
        adjuster = clang::tooling::combineAdjusters(
            std::move(adjuster),
            clang::tooling::getInsertArgumentAdjuster(
                args_after, clang::tooling::ArgumentInsertPosition::END));
        adjusting_db->appendArgumentsAdjuster(adjuster);
        return adjusting_db;
      }();

      const std::vector<std::string> &raw_sources = db->getAllFiles();
      std::vector<std::filesystem::path> source_files(raw_sources.size());
      std::transform(
          raw_sources.begin(), raw_sources.end(), source_files.begin(),
          [](const std::string &s) { return std::filesystem::path(s); });

      auto result = build_graph::from_compilation_db(
          *db, std::filesystem::current_path(), source_files, map_ext, fs,
          build_graph::options().enable_replace_file_optimization(
              smaller_file_opt));

      out << format_time(timer.restart()) << " # seconds to parse files\n";
      return result;
    }
  }();

  if (!result) {
    // TODO: error message
    err << "Error";
    return 1;
  }

  const auto &graph = result->graph;
  const auto &sources = result->sources;
  const auto &missing = result->missing_includes;
  const auto &unguarded = result->unguarded_files;

  if (show_sources.getValue()) {
    out << "  sources:\n";
    for (Graph::vertex_descriptor v : sources) {
      out << "    - " << format_str(graph[v].path.string()) << "\n";
    }
  } else {
    out << "  # sources: pass --show-sources to list source files\n";
  }
  out << "  source count: " << sources.size() << "\n";
  out << "  file count: " << num_vertices(graph) << "\n";
  out << "  include directives: " << num_edges(graph) << "\n";

  const get_total_cost::result naive_cost = get_naive_cost(graph);
  const get_total_cost::result project_cost =
      get_total_cost::from_graph(graph, sources);

  const cost &postprocessed = project_cost.true_cost;
  const cost &actual = project_cost.total();

  out << "  # These are the stats of all the files found.  This would be\n"
         "  # similar to the cost of a \"unity build\".\n"
      << "  preprocessed:\n"
      << "    byte count: " << naive_cost.total().file_size << " # "
      << format_file_size(naive_cost.total().file_size) << "\n"
      << "    token count: " << naive_cost.total().token_count << "\n"
      << "  # These are the stats of all postprocessed\n"
         "  # translation units passed to the compiler.\n"
      << "  postprocessed:\n"
      << "    byte count: " << postprocessed.file_size << " # "
      << format_file_size(postprocessed.file_size) << '\n'
      << "    token count: " << postprocessed.token_count << '\n'
      << "  # These are the stats of the actual build, i.e. all\n"
         "  # postprocessed translation units passed to the compiler "
         "subtracting the\n"
         "  # cost of precompiled header:\n"
      << "  actual:\n"
      << "    byte count: " << actual.file_size << " # "
      << format_file_size(actual.file_size) << "\n"
      << "    token count: " << actual.token_count << "\n";

  timer.restart();

  out << "  missing files:";
  if (missing.empty()) {
    out << " []\n";
  } else {
    out << "\n";
    for (std::string_view m : missing) {
      out << "    - " << format_str(m) << "\n";
    }
  }

  if (!save_path.empty()) {
    out << "  output:\n";
    out << "    file: " << format_str(save_path.getValue()) << "\n";
    out << "    save time: ";
    std::ofstream ofs(save_path.getValue()); // save to file
    boost::archive::text_oarchive oa(ofs);
    out << format_time(timer.restart())
        << " # seconds to serialize and save graph\n";
  }

  if (analyze.getValue()) {
    out << "analysis:\n";
    out << "  # Below are the files that do not have an include guard or "
           "have an\n"
        << "  # include guard that is not strict enough to enable the "
           "multiple-include\n"
        << "  # optimization where compilers will skip opening a file a "
           "second time\n"
        << "  # for each source.\n"
        << "  unguarded files:\n"
        << "    time taken: 0 # seconds\n"
        << "    results:";
    if (unguarded.empty()) {
      out << " []\n";
    } else {
      out << "\n";
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
      std::transform(unguarded_copy.begin(), unguarded_copy.end(),
                     files.begin(), [&](Graph::vertex_descriptor v) {
                       std::ostringstream out;
                       out << "      - file: " << pretty_path(graph[v]) << "\n"
                           << "        count: " << in_degree(v, graph) << "\n";
                       return out.view();
                     });
      std::copy(files.begin(), files.end(),
                std::ostream_iterator<std::string>(out));
    }

    {
      std::vector<component_and_cost> results =
          find_unused_components::from_graph(graph, sources, 0u);
      out << "  # These are the components that have a header file that is "
             "not included\n"
          << "  # by any other component and may be a candidate for "
             "removal.\n"
          << "  unreferenced components:\n"
          << "    time taken: " << format_time(timer.restart())
          << " # seconds\n"
          << "    results:";
      if (results.empty()) {
        out << " []\n";
      } else {
        out << "\n";
      }
      std::sort(results.begin(), results.end(),
                [](const component_and_cost &l, const component_and_cost &r) {
                  return l.saving.token_count > r.saving.token_count;
                });
      for (const component_and_cost &i : results) {
        const double percentage =
            (100.0 * i.saving.token_count) / project_cost.true_cost.token_count;
        out << "      - source: " << pretty_path(*i.source) << "\n"
            << "        token count: " << i.saving.token_count << " # ("
            << percentage << "%)\n";
      }
    }
    {
      std::vector<include_directive_and_cost> results =
          find_expensive_includes::from_graph(
              graph, sources,
              project_cost.true_cost.token_count * percent_cut_off);
      out << "  # This is a list of the most costly #include directives.\n"
             "  include directives:\n"
             "    time: "
          << format_time(timer.restart())
          << " # seconds\n"
             "      results: ";
      std::sort(results.begin(), results.end(),
                [](const include_directive_and_cost &l,
                   const include_directive_and_cost &r) {
                  return l.saving.token_count > r.saving.token_count;
                });
      if (results.empty()) {
        out << "[]\n";
      } else {
        out << "\n";
        for (const include_directive_and_cost &i : results) {
          const double percentage = (100.0 * i.saving.token_count) /
                                    project_cost.true_cost.token_count;
          out << "    - directive: " << format_str(i.include->code) << "\n"
              << "      file: " << format_str(i.file.filename().string())
              << "\n"
              << "      line: " << i.include->lineNumber << "\n"
              << "      token count: " << i.saving.token_count << " # "
              << percentage << "%\n";
        }
      }
    }

    {
      std::vector<find_expensive_headers::result> results =
          find_expensive_headers::from_graph(
              graph, sources,
              project_cost.true_cost.token_count * percent_cut_off);
      out << "  # This is a list of all header files that should be "
             "considered\n"
             "  # to not be included by other header files, but source files "
             "only:\n"
             "  make private: \n"
             "    time: "
          << format_time(timer.restart())
          << " # seconds\n"
             "    results: ";
      if (results.empty()) {
        out << "[]\n";
      } else {
        out << "\n";
        std::sort(results.begin(), results.end(),
                  [](const find_expensive_headers::result &l,
                     const find_expensive_headers::result &r) {
                    return l.total_saving().token_count >
                           r.total_saving().token_count;
                  });
        for (const find_expensive_headers::result &i : results) {
          const double percentage = (100.0 * i.total_saving().token_count) /
                                    project_cost.true_cost.token_count;
          out << "    - file: " << format_str(graph[i.v].path.string())
              << "\n"
                 "      reference count: "
              << graph[i.v].internal_incoming
              << "\n"
                 "      token count: "
              << i.total_saving().token_count << " # " << std::setprecision(2)
              << std::fixed << percentage << "%\n";
        }
      }
    }

    {
      std::vector<recommend_precompiled::result> results =
          recommend_precompiled::from_graph(graph, sources,
                                            project_cost.true_cost.token_count *
                                                percent_cut_off,
                                            pch_ratio.getValue());
      out << "  # This is a list of all header files that should be "
             "considered\n"
             "  # to be added to the precompiled header:\n"
             "  pch additions:\n"
             "    time: "
          << format_time(timer.restart())
          << " # seconds\n"
             "    results: ";
      if (results.empty()) {
        out << "[]\n";
      } else {
        out << "\n";
        std::sort(results.begin(), results.end(),
                  [](const recommend_precompiled::result &l,
                     const recommend_precompiled::result &r) {
                    return l.saving.token_count > r.saving.token_count;
                  });
        for (const recommend_precompiled::result &i : results) {
          const double percentage = (100.0 * i.saving.token_count) /
                                    project_cost.true_cost.token_count;
          out << "    - file: " << format_str(graph[i.v].path.string()) << "\n"
              << "      token count: " << i.saving.token_count << " # "
              << std::setprecision(2) << std::fixed << percentage << "%\n";
        }
      }
    }

    {
      // Assume that each "expensive" file could be reduced this much
      const double assumed_reduction = 0.50;
      std::vector<file_and_cost> results = find_expensive_files::from_graph(
          graph, sources,
          project_cost.true_cost.token_count * percent_cut_off /
              assumed_reduction);
      out << "  # This is a list of all comparatively large files that\n"
             "  # should be considered to be simplified or split into\n"
             "  # smaller parts and #includes updated:\n"
             "  large files:\n"
             "    time: "
          << format_time(timer.restart()) << " # seconds\n"
          << "    assumed_reduction: " << assumed_reduction * 100
          << " # %\n"
             "    results: ";
      if (results.empty()) {
        out << "[]\n";
      } else {
        out << "\n";
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
          out << "    - file: " << format_str(i.node->path.string()) << "\n"
              << "      token count: " << saving << " # "
              << std::setprecision(2) << std::fixed << saving << " ("
              << percentage << "%\n";
        }
      }
    }

    {
      std::vector<find_unnecessary_sources::result> results =
          find_unnecessary_sources::from_graph(
              graph, sources,
              project_cost.true_cost.token_count * percent_cut_off);
      out << "  # This is a list of all source files that should be "
             "considered\n"
             "  # to be inlined into the header and then the source file "
             "removed:\n"
             "  inline sources:\n"
             "    time: "
          << format_time(timer.restart())
          << " # seconds\n"
             "    results: ";
      if (results.empty()) {
        out << "[]\n";
      } else {
        out << "\n";
        std::sort(results.begin(), results.end(),
                  [](const find_unnecessary_sources::result &l,
                     const find_unnecessary_sources::result &r) {
                    return l.total_saving().token_count >
                           r.total_saving().token_count;
                  });
        for (const find_unnecessary_sources::result &i : results) {
          const double percentage = (100.0 * i.total_saving().token_count) /
                                    project_cost.true_cost.token_count;
          out << "    - source: " << format_str(graph[i.source].path.string()) << "\n"
              << "      token count: " << i.total_saving().token_count << " # "
              << std::setprecision(2) << std::fixed << percentage << "% \n";
        }
      }
    }
  }
  return 0;
}

} // namespace IncludeGuardian