#include "expassign/constraints/bool_constraint.hpp"

#include "expassign/request.hpp"

namespace expassign {

bool BoolConstraint::Matches(const Request& request) const {
  auto it = request.bools.find(Property());
  return it != request.bools.end() && it->second == expected_;
}

}  // namespace expassign
