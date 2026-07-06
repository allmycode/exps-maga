#include "expassign/constraints/string_constraint.hpp"

#include "expassign/request.hpp"

namespace expassign {

StringConstraint::StringConstraint(std::string property,
                                   std::vector<std::string> values,
                                   bool negated)
    : IConstraint(std::move(property)),
      values_(std::make_move_iterator(values.begin()),
              std::make_move_iterator(values.end())),
      negated_(negated) {}

bool StringConstraint::Matches(const Request& request) const {
  auto it = request.strings.find(Property());
  if (it == request.strings.end()) return false;
  return values_.contains(it->second) != negated_;
}

}  // namespace expassign
