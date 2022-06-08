#include "dot_graph.hpp"

#include <boost/graph/graphviz.hpp>

#include <filesystem>
#include <string>

namespace IncludeGuardian {
namespace {

std::string to2Digit(int x) {
  if (x < 10) {
    return "0" + std::to_string(x);
  } else {
    return std::to_string(x);
  }
}

std::string colorForSize(off_t bytes) {
  const double log_e =
      std::clamp(std::log(static_cast<double>(bytes)) / 6.0, 1.0, 2.0);
  const double red = 99.0 * (log_e - 1.0);
  const double green = 99.0 - red;
  return "#" + to2Digit(red) + to2Digit(green) + "00";
  return "#990000";
}

int fontSizeForFileSize(off_t bytes) {
  return 7.0 + 3.0 * std::log(static_cast<double>(bytes));
}

} // namespace

void dot_graph::print(const Graph &graph, std::ostream &stream) {
  struct {
    const Graph &m_graph;
    void operator()(std::ostream &out, Graph::vertex_descriptor v) const {
      const file_node &n = m_graph[v];
      std::filesystem::path p(n.path);
      out << "[label=\"" << p.filename().string()
          << "\"]"
             "[style=\"filled\"]"
             "[fontcolor=\"#ffffff\"]"
             "[fillcolor=\""
          << colorForSize(n.fileSizeInBytes)
          << "\"]"
             "[fontsize=\""
          << fontSizeForFileSize(n.fileSizeInBytes) << "pt\"]";
    }
  } visitor{graph};
  write_graphviz(stream, graph, visitor);
}

} // namespace IncludeGuardian