#ifndef INCLUDE_GUARD_0B0B1CC2_10E3_40D7_8E84_6C5803D3E5ED
#define INCLUDE_GUARD_0B0B1CC2_10E3_40D7_8E84_6C5803D3E5ED

#include <iosfwd>

namespace IncludeGuardian {

// Run `includeguardian` with the array of command line options specified
// by the array `argv` of length `argc`.  Output the results to `out` and
// any errors to `err`.  Return 0 on success and non-zero on error.
int run(int argc, const char **argv, std::ostream &out, std::ostream &err);

} // namespace IncludeGuardian

#endif