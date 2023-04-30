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
#include "topological_order.hpp"

#include <termcolor/termcolor.hpp>

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

struct percent {
  double value;

  percent(double p) : value(p) {}
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

#define key_color termcolor::bright_blue
#define str_color &termcolor::bright_yellow
#define num_color &termcolor::bright_red
#define punc_color &termcolor::bright_white
#define comment_color &termcolor::green

void yaml_value(std::ostream &o, int i) { o << num_color << i << '\n'; }

void yaml_value(std::ostream &o, percent p) {
  o << num_color << std::setprecision(2) << std::fixed << p.value
    << comment_color << " # (%)\n";
}

void yaml_value(std::ostream &o, std::chrono::steady_clock::duration d) {
  const double ms = duration_cast<std::chrono::milliseconds>(d).count();
  const double s = std::round(ms) / 1000;
  o << num_color << s << comment_color << " # seconds\n";
}

void yaml_value(std::ostream &o, cost d) {
  o << num_color << d.token_count << '\n';
}

void yaml_value(std::ostream &o, std::string_view s) {
  if (std::any_of(s.begin(), s.end(),
                  [](char c) { return c == '\\' || c == '"' || c == '#'; })) {
    // If we contain a backslash or a double quote, then we need to
    // surround with single quotes and double up any single quotes.
    // This does not support non-printable characters, which need
    // to be surrounded by double quotes.
    // We also surround strings that contain a `#` because VisualStudio
    // code highlights these as a comment
    o << punc_color << '\'' << str_color;
    auto it = s.begin();
    while (true) {
      auto next = std::find(it, s.end(), '\'');
      o << std::string_view(it, next);
      if (next == s.end()) {
        break;
      }
      o << "''";
      it = next;
    }
    o << punc_color << "'\n";
  } else {
    o << str_color << s << '\n';
  }
}

void yaml_value(std::ostream &o,
                boost::units::quantity<boost::units::information::info> i) {
  o << num_color << std::setprecision(std::numeric_limits<double>::digits10)
    << i.value() << comment_color << " # " << format_file_size(i) << "\n";
}

void yaml_value(std::ostream &o, const file_node &v) {
  std::ostringstream ss;
  if (v.is_external) {
    ss << '<';
    ss << v.path.string();
    ss << '>';
  } else {
    ss << '"';
    ss << v.path.string();
    ss << '"';
  }
  yaml_value(o, ss.view());
}

class ObjPrinter;

class ArrayPrinter {
  std::ostream &o;
  int m_indent;
  int m_num_entries = 0;

public:
  ArrayPrinter(std::ostream &out, int indent) : o(out), m_indent(indent) {}

  ArrayPrinter(ArrayPrinter &&other)
      : o(other.o), m_indent(other.m_indent),
        m_num_entries(std::exchange(other.m_num_entries, -1)) {}

  ~ArrayPrinter() {
    if (m_num_entries == 0) {
      o << "[]\n";
    }
  }

  ObjPrinter obj();

  ArrayPrinter arr(std::string_view key) {
    if (m_num_entries++ == 0) {
      o << "\n";
    }
    std::fill_n(std::ostream_iterator<char>(o), 2 * m_indent, ' ');
    o << punc_color << "- " << key_color << key << punc_color << ":";
    return ArrayPrinter(o, m_indent + 1);
  }

  template <typename T> void value(const T &t) {
    if (m_num_entries++ == 0) {
      o << "\n";
    }
    std::fill_n(std::ostream_iterator<char>(o), 2 * m_indent, ' ');
    o << punc_color << "- ";
    yaml_value(o, t);
  }
};

class ObjPrinter {
  std::ostream &o;
  struct Indent {
    int m_indent;
    bool m_first_arr_elem;

    friend std::ostream &operator<<(std::ostream &out, Indent &i) {
      if (i.m_first_arr_elem) {
        std::fill_n(std::ostream_iterator<char>(out), 2 * (i.m_indent - 1),
                    ' ');
        out << punc_color << "- ";
        i.m_first_arr_elem = false;
      } else {
        std::fill_n(std::ostream_iterator<char>(out), 2 * i.m_indent, ' ');
      }
      return out;
    }
  };

  Indent indent;

public:
  ObjPrinter(std::ostream &out, int indent, bool in_array = false)
      : o(out), indent{indent, in_array} {}

  void comment(std::string_view comment) {
    o << indent << comment_color << "# " << comment << '\n';
  }

  void key(std::string_view str) {
    o << indent << key_color << str << punc_color << ": ";
  }

  ObjPrinter obj(std::string_view str) {
    key(str);
    o << "\n";
    return ObjPrinter(o, indent.m_indent + 1);
  }

  ArrayPrinter arr(std::string_view str) {
    key(str);
    return ArrayPrinter(o, indent.m_indent + 1);
  }

  template <typename T> void value(const T &t) { yaml_value(o, t); }

  template <typename T> void property(std::string_view k, const T &v) {
    key(k);
    value(v);
  }
};

ObjPrinter ArrayPrinter::obj() {
  if (m_num_entries++ == 0) {
    o << "\n";
  }
  return ObjPrinter(o, m_indent + 1, true);
}

ObjPrinter start_document(std::ostream &out) {
  out << punc_color << "---\n";
  return ObjPrinter(out, 0);
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
  std::filesystem::path m_working_directory; //< Directory of includeguardian
  std::vector<std::filesystem::path> m_sources;

  ReplacementCompilationDatabase(const std::filesystem::path &working_directory,
                                 const std::filesystem::path &source_directory,
                                 llvm::vfs::FileSystem &fs)
      : m_working_directory(working_directory), m_sources() {
    std::vector<std::string> directories;
    directories.push_back(source_directory.string());
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
      llvm::cl::value_desc("enabled"), llvm::cl::init(true),
      llvm::cl::cat(build_category));

  llvm::cl::OptionCategory topological_category("Topological Order Options");
  llvm::cl::opt<bool> topological_order(
      "topological-order",
      llvm::cl::desc("Whether to display the files found in topological order"),
      llvm::cl::value_desc("enabled"), llvm::cl::init(false),
      llvm::cl::cat(topological_category));

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
  ObjPrinter root = start_document(out);

  // Use direct printing to get underlined links
  out << comment_color << "# Visit " << termcolor::underline
      << "https://includeguardian.io" << termcolor::reset << comment_color
      << " for updates and\n"
      << "# " << termcolor::underline << "https://includeguardian.io/ci"
      << termcolor::reset << comment_color
      << " to keep your project building fast!\n";

  ObjPrinter stats = root.obj("stats");
  {
    stats.property("version", INCLUDEGUARDIAN_VERSION);
    stats.property("command",
                   std::accumulate(argv, argv + argc, std::string(""),
                                   [](std::string &&ss, const char *arg) {
                                     if (ss.empty()) {
                                       return std::string(arg);
                                     }
                                     ss.append(" ");
                                     ss.append(arg);
                                     return std::move(ss);
                                   }));
  }

  build_graph::options options;
  options.enable_replace_file_optimization(smaller_file_opt);
  std::optional<ArrayPrinter> sources_printer;
  if (show_sources.getValue()) {
    sources_printer.emplace(stats.arr("sources"));
    options.source_started = [&](const std::filesystem::path &source) {
      sources_printer->value(source.string());
    };
  } else {
    stats.comment("sources: pass --show-sources to list source files");
  }

  auto result = [&]() -> llvm::Expected<build_graph::result> {
    if (!load_path.empty()) {
      build_graph::result r;
      std::ifstream ifs(load_path.getValue()); // save to file
      boost::archive::text_iarchive ia(ifs);
      ia >> r;
      if (options.source_started) {
        std::for_each(r.sources.begin(), r.sources.end(),
                      [&](const Graph::vertex_descriptor source) {
                        options.source_started(r.graph[source].path);
                      });
      }
      sources_printer.reset();
      stats.property("processing time", timer.restart());
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
            std::filesystem::current_path(),
            std::filesystem::current_path() / fake_compilation_db.getValue(),
            *fs);
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

      auto result =
          build_graph::from_compilation_db(*db, std::filesystem::current_path(),
                                           source_files, map_ext, fs, options);

      sources_printer.reset();
      stats.property("processing time", timer.restart());
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

  stats.property("source count", sources.size());
  stats.property("file count", num_vertices(graph));
  stats.property("include directives", num_edges(graph));

  const get_total_cost::result naive_cost = get_naive_cost(graph);
  const get_total_cost::result project_cost =
      get_total_cost::from_graph(graph, sources);

  const cost &postprocessed = project_cost.true_cost;
  const cost &actual = project_cost.total();

  {
    stats.comment("These are the stats of all the files found.  This would be");
    stats.comment("similar to the cost of a \"unity build\".");
    ObjPrinter o = stats.obj("preprocessed");
    o.property("byte count", naive_cost.total().file_size);
    o.property("token count", naive_cost.total().token_count);
  }
  {
    stats.comment("These are the stats of all postprocessed");
    stats.comment("translation units passed to the compiler.");
    ObjPrinter o = stats.obj("postprocessed");
    o.property("byte count", postprocessed.file_size);
    o.property("token count", postprocessed.token_count);
  }
  {
    stats.comment("These are the stats of the actual build, i.e. all");
    stats.comment("postprocessed translation units passed to the compiler "
                  "subtracting the");
    stats.comment("cost of precompiled header:");
    ObjPrinter o = stats.obj("actual");
    o.property("byte count", actual.file_size);
    o.property("token count", actual.token_count);
  }

  timer.restart();

  {
    ArrayPrinter missing_files = stats.arr("missing files");
    for (std::string_view m : missing) {
      missing_files.value(m);
    }
  }

  if (!save_path.empty()) {
    ObjPrinter output = stats.obj("output");
    output.property("file", save_path.getValue());
    output.key("save time");
    std::ofstream ofs(save_path.getValue()); // save to file
    boost::archive::text_oarchive oa(ofs);
    oa << *result;
    output.value(timer.restart());
  }

  if (analyze.getValue()) {
    out << '\n';
    ObjPrinter an = root.obj("analysis");

    {
      an.comment("Below are the files that do not have an include guard or");
      an.comment("include guard that is not strict enough to enable the "
                 "multiple-include");
      an.comment("optimization where compilers will skip opening a file a "
                 "second time");
      an.comment("for each source.");
      ObjPrinter unguarded_files = an.obj("unguarded files");
      std::vector<Graph::vertex_descriptor> unguarded_copy;
      std::copy_if(unguarded.begin(), unguarded.end(),
                   std::back_inserter(unguarded_copy),
                   [&](const Graph::vertex_descriptor v) {
                     return !graph[v].is_external && in_degree(v, graph) > 1;
                   });
      std::sort(unguarded_copy.begin(), unguarded_copy.end(),
                [&](Graph::vertex_descriptor l, Graph::vertex_descriptor r) {
                  return in_degree(l, graph) > in_degree(r, graph);
                });
      unguarded_files.property("time taken", timer.restart());

      ArrayPrinter results = unguarded_files.arr("results");
      std::for_each(unguarded_copy.begin(), unguarded_copy.end(),
                    [&](Graph::vertex_descriptor v) {
                      ObjPrinter result = results.obj();
                      result.property("file", graph[v]);
                      result.property("count", in_degree(v, graph));
                    });
    }

    {
      out << '\n';
      an.comment(
          "These are components that have a header file that is not included");
      an.comment("by any other component and may be a candidate for removal.");
      ObjPrinter unreferenced = an.obj("unreferenced components");

      // Use an arbitrary minimum size because unused components can have a
      // small amount of code that we shouldn't care about too much as long as
      // it's trivial.
      const int minimum_size = 10;
      std::vector<component_and_cost> results =
          find_unused_components::from_graph(graph, sources, 0u, minimum_size);
      std::sort(results.begin(), results.end(),
                [](const component_and_cost &l, const component_and_cost &r) {
                  return l.saving.token_count > r.saving.token_count;
                });

      unreferenced.property("time taken", timer.restart());
      ArrayPrinter results_out = unreferenced.arr("results");
      for (const component_and_cost &i : results) {
        ObjPrinter result = results_out.obj();
        result.property("source", *i.source);
        result.property("saving", percent((100.0 * i.saving.token_count) /
                                          project_cost.true_cost.token_count));
      }
    }
    {
      out << '\n';
      an.comment("This is a list of the most costly #include directives.");
      ObjPrinter include_directives = an.obj("include directives");
      std::vector<include_directive_and_cost> results =
          find_expensive_includes::from_graph(
              graph, sources,
              project_cost.true_cost.token_count * percent_cut_off);
      std::sort(results.begin(), results.end(),
                [](const include_directive_and_cost &l,
                   const include_directive_and_cost &r) {
                  return l.saving.token_count > r.saving.token_count;
                });
      include_directives.property("time", timer.restart());

      ArrayPrinter results_out = include_directives.arr("results");
      for (const include_directive_and_cost &i : results) {
        ObjPrinter result_out = results_out.obj();
        result_out.property("directive", "#include " + i.include->code);
        result_out.property("file", i.file.filename());
        result_out.property("line", i.include->lineNumber);
        result_out.property("saving",
                            percent((100.0 * i.saving.token_count) /
                                    project_cost.true_cost.token_count));
      }
    }

    {
      out << '\n';
      an.comment(
          "This is a list of all header files that should be considered");
      an.comment(
          "to not be included by other header files, but source files only");
      ObjPrinter make_private = an.obj("make private");
      std::vector<find_expensive_headers::result> results =
          find_expensive_headers::from_graph(
              graph, sources,
              project_cost.true_cost.token_count * percent_cut_off);
      std::sort(results.begin(), results.end(),
                [](const find_expensive_headers::result &l,
                   const find_expensive_headers::result &r) {
                  return l.saving.token_count > r.saving.token_count;
                });
      make_private.property("time", timer.restart());

      ArrayPrinter results_out = make_private.arr("results");
      for (const find_expensive_headers::result &i : results) {
        ObjPrinter result_out = results_out.obj();
        result_out.property("file", graph[i.v]);
        result_out.property("reference count", i.header_reference_count);
        result_out.property("saving",
                            percent((100.0 * i.saving.token_count) /
                                    project_cost.true_cost.token_count));
      }
    }

    {
      out << '\n';
      an.comment(
          "This is a list of all header files that should be considered");
      an.comment("to be added to the precompiled header:");
      ObjPrinter pch_additions = an.obj("pch additions");
      std::vector<recommend_precompiled::result> results =
          recommend_precompiled::from_graph(graph, sources,
                                            project_cost.true_cost.token_count *
                                                percent_cut_off,
                                            pch_ratio.getValue());
      std::sort(results.begin(), results.end(),
                [](const recommend_precompiled::result &l,
                   const recommend_precompiled::result &r) {
                  return l.saving.token_count > r.saving.token_count;
                });
      pch_additions.property("time", timer.restart());

      ArrayPrinter results_out = pch_additions.arr("results");
      for (const recommend_precompiled::result &i : results) {
        ObjPrinter result_out = results_out.obj();
        result_out.property("file", graph[i.v]);
        result_out.property("saving",
                            percent((100.0 * i.saving.token_count) /
                                    project_cost.true_cost.token_count));
      }
    }

    {
      out << '\n';
      an.comment("This is a list of all comparatively large files that");
      an.comment("should be considered to be simplified or split into");
      an.comment("smaller parts and #includes updated:");
      ObjPrinter large_files = an.obj("large files");

      // Assume that each "expensive" file could be reduced this much
      const double assumed_reduction = 0.50;
      std::vector<file_and_cost> results = find_expensive_files::from_graph(
          graph, sources,
          project_cost.true_cost.token_count * percent_cut_off /
              assumed_reduction);
      large_files.property("assumed reduction",
                           percent(assumed_reduction * 100));
      std::sort(
          results.begin(), results.end(),
          [](const file_and_cost &l, const file_and_cost &r) {
            return static_cast<std::size_t>(l.node->true_cost().token_count) *
                       l.sources >
                   static_cast<std::size_t>(r.node->true_cost().token_count) *
                       r.sources;
          });
      large_files.property("time", timer.restart());

      ArrayPrinter results_out = large_files.arr("results");
      for (const file_and_cost &i : results) {
        const unsigned saving =
            i.sources * assumed_reduction * i.node->true_cost().token_count;
        ObjPrinter result_out = results_out.obj();
        result_out.property("file", i.node->path);
        result_out.property(
            "saving",
            percent((100.0 * saving) / project_cost.true_cost.token_count));
      }
    }

    {
      out << '\n';
      an.comment(
          "This is a list of all source files that should be considered");
      an.comment(
          "to be inlined into the header and then the source file removed:");
      ObjPrinter inline_sources = an.obj("inline sources");

      std::vector<find_unnecessary_sources::result> results =
          find_unnecessary_sources::from_graph(
              graph, sources,
              project_cost.true_cost.token_count * percent_cut_off);
      std::sort(results.begin(), results.end(),
                [](const find_unnecessary_sources::result &l,
                   const find_unnecessary_sources::result &r) {
                  return l.total_saving().token_count >
                         r.total_saving().token_count;
                });
      inline_sources.property("time", timer.restart());

      ArrayPrinter results_out = inline_sources.arr("results");
      for (const find_unnecessary_sources::result &i : results) {
        ObjPrinter result_out = results_out.obj();
        result_out.property("source", graph[i.source].path);
        result_out.property("saving",
                            percent((100.0 * i.total_saving().token_count) /
                                    project_cost.true_cost.token_count));
      }
    }
  }

  if (topological_order.getValue()) {
    out << '\n';
    ArrayPrinter top = root.arr("topological order");
    std::vector<std::vector<std::vector<Graph::vertex_descriptor>>> ordering =
        topological_order::from_graph(graph, sources);
    for (std::size_t level = 0; level < ordering.size(); ++level) {
      ObjPrinter result_out = top.obj();
      result_out.property("level", level);
      ArrayPrinter files = result_out.arr("files");
      const std::vector<std::vector<Graph::vertex_descriptor>> &groups =
          ordering[level];
      for (const std::vector<Graph::vertex_descriptor> &group : groups) {
        if (group.size() == 1) {
          files.value(graph[group.front()]);
        } else {
          ArrayPrinter group_out = files.arr("cycle");
          for (const Graph::vertex_descriptor v : group) {
            group_out.value(graph[v]);
          }
        }
      }
    }
  }

  out << termcolor::reset;
  return 0;
}

} // namespace IncludeGuardian
