#include "cost.hpp"

#include <boost/units/io.hpp>

#include <iomanip>
#include <ostream>

namespace IncludeGuardian {

cost::cost() : cost{0, 0.0 * boost::units::information::bytes} {}

cost::cost(
    const int token_count,
    const boost::units::quantity<boost::units::information::info> file_size)
    : token_count{token_count}, file_size{file_size} {}

std::ostream &operator<<(std::ostream &stream, cost c) {
  return stream << '{' << std::setprecision(2) << std::fixed << c.token_count
                << ", " << c.file_size << '}';
}

bool operator==(cost lhs, cost rhs) {
  return lhs.file_size == rhs.file_size && lhs.token_count == rhs.token_count;
}

bool operator!=(cost lhs, cost rhs) { return !(lhs == rhs); }

cost &operator+=(cost &lhs, cost rhs) {
  lhs.file_size += rhs.file_size;
  lhs.token_count += rhs.token_count;
  return lhs;
}

cost &operator-=(cost &lhs, cost rhs) {
  lhs.file_size -= rhs.file_size;
  lhs.token_count -= rhs.token_count;
  return lhs;
}

cost operator+(cost lhs, cost rhs) { return lhs += rhs; }

cost operator-(cost lhs, cost rhs) { return lhs -= rhs; }

} // namespace IncludeGuardian
