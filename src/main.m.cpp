#include "reachability_graph.hpp"
#include <boost/graph/adjacency_list.hpp>
#include <iostream>
#include <string>

using namespace IncludeGuardian;

int main() {
    boost::adjacency_list<
        boost::vecS,
        boost::vecS,
        boost::directedS,
        std::string> dag;
    const auto a = boost::add_vertex("a", dag);
    const auto b = boost::add_vertex("b", dag);
    const auto c = boost::add_vertex("c", dag);
    const auto d = boost::add_vertex("d", dag);
    const auto e = boost::add_vertex("e", dag);
    const auto f = boost::add_vertex("f", dag);
    const auto g = boost::add_vertex("g", dag);
    const auto h = boost::add_vertex("h", dag);
    const auto z = boost::add_vertex("i", dag);
    boost::add_edge(a, c, dag);
    boost::add_edge(a, d, dag);
    boost::add_edge(b, d, dag);
    boost::add_edge(b, e, dag);
    boost::add_edge(c, h, dag);
    boost::add_edge(d, f, dag);
    boost::add_edge(e, g, dag);
    boost::add_edge(g, h, dag);
    boost::add_edge(f, h, dag);

    reachability_graph reach(dag);

    std::cout << "There are " << reach.number_of_paths(a, h)
        << " paths from A to H";
    return 0;
}