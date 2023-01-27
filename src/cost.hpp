#ifndef INCLUDE_GUARD_C6A8FB0D_279F_4CAA_A2C3_FDCE8606C2EE
#define INCLUDE_GUARD_C6A8FB0D_279F_4CAA_A2C3_FDCE8606C2EE

#include <boost/units/quantity.hpp>
#include <boost/units/systems/information/byte.hpp>

namespace IncludeGuardian {

struct cost {
  std::int64_t token_count;
  boost::units::quantity<boost::units::information::info> file_size;

  cost();

  cost(std::int64_t token_count,
       boost::units::quantity<boost::units::information::info> file_size);

  template <typename Archive>
  void serialize(Archive &ar, const unsigned version) {
    ar &token_count;
    ar &file_size;
  }
};

std::ostream &operator<<(std::ostream &stream, cost c);

bool operator==(cost lhs, cost rhs);
bool operator!=(cost lhs, cost rhs);

cost operator-(cost v);

cost &operator+=(cost &lhs, cost rhs);
cost &operator-=(cost &lhs, cost rhs);
cost operator+(cost lhs, cost rhs);
cost operator-(cost lhs, cost rhs);
cost operator*(cost lhs, int rhs);
cost operator*(int lhs, cost rhs);

} // namespace IncludeGuardian

#endif