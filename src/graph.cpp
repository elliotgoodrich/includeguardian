#include "graph.hpp"

#include <boost/units/io.hpp>

#include <ostream>

namespace IncludeGuardian {

file_node::file_node() = default;

file_node::file_node(const std::filesystem::path &path) : path(path) {}

file_node &file_node::with_cost(
    std::int64_t token_count,
    boost::units::quantity<boost::units::information::info> file_size) & {
  underlying_cost = cost{token_count, file_size};
  return *this;
}

file_node &&file_node::with_cost(
    std::int64_t token_count,
    boost::units::quantity<boost::units::information::info> file_size) && {
  underlying_cost = cost{token_count, file_size};
  return std::move(*this);
}

file_node &file_node::with_cost(cost c) & {
  underlying_cost = c;
  return *this;
}
file_node &&file_node::with_cost(cost c) && {
  underlying_cost = c;
  return std::move(*this);
}

file_node &file_node::set_external(bool v) & {
  is_external = v;
  return *this;
}

file_node &&file_node::set_external(bool v) && {
  is_external = v;
  return std::move(*this);
}

file_node &file_node::set_internal_parents(unsigned v) & {
  this->internal_incoming = v;
  return *this;
}

file_node &&file_node::set_internal_parents(unsigned v) && {
  this->internal_incoming = v;
  return std::move(*this);
}

file_node &file_node::set_external_parents(unsigned v) & {
  this->external_incoming = v;
  return *this;
}

file_node &&file_node::set_external_parents(unsigned v) && {
  this->external_incoming = v;
  return std::move(*this);
}

file_node &file_node::set_precompiled(bool v) & {
  this->is_precompiled = v;
  return *this;
}

file_node &&file_node::set_precompiled(bool v) && {
  this->is_precompiled = v;
  return std::move(*this);
}

file_node &file_node::set_guarded(bool v) & {
  this->is_guarded = v;
  return *this;
}

file_node &&file_node::set_guarded(bool v) && {
  this->is_guarded = v;
  return std::move(*this);
}

cost file_node::true_cost() const {
  return is_precompiled ? cost{} : underlying_cost;
}

std::ostream &operator<<(std::ostream &stream, const file_node &value) {
  return stream << value.path << ' ' << value.underlying_cost
                << " [incoming (int)=" << value.internal_incoming << ']'
                << " [incoming (ext)=" << value.external_incoming << ']'
                << (value.is_external ? " [external]" : "")
                << (value.component ? " [linked]" : "")
                << (value.is_precompiled ? " [precompiled]" : "");
}

std::ostream &operator<<(std::ostream &stream, const include_edge &value) {
  return stream << value.code << "#" << value.lineNumber
                << (value.is_removable ? "" : " not removable");
}

bool operator==(const include_edge &lhs, const include_edge &rhs) {
  return lhs.code == rhs.code && lhs.lineNumber == rhs.lineNumber &&
         lhs.is_removable == rhs.is_removable;
}

bool operator!=(const include_edge &lhs, const include_edge &rhs) {
  return !(lhs == rhs);
}

} // namespace IncludeGuardian
