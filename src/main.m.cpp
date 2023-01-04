#include "includeguardian.hpp"

#include <iostream>

int main(int argc, const char **argv) {
  std::ios::sync_with_stdio(false);
  return IncludeGuardian::run(argc, argv, std::cout, std::cerr);
}