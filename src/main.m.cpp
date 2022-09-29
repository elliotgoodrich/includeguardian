#include "includeguardian.hpp"

#include <iostream>

int main(int argc, const char **argv) {

  // Use the user's locale to format numbers etc.
  std::cout.imbue(std::locale(""));

  return IncludeGuardian::run(argc, argv, std::cout, std::cerr);
}