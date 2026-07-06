#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "expassign/constraints/constraint.hpp"

namespace expassign {

// Ограничение на строковое свойство: значение должно входить в заданный набор
// строк (negated == false) либо, наоборот, не входить в него (negated == true).
class StringConstraint final : public IConstraint {
 public:
  StringConstraint(std::string property, std::vector<std::string> values,
                   bool negated = false);

  PropertyType Type() const override { return PropertyType::kString; }
  bool Matches(const Request& request) const override;

  const std::unordered_set<std::string>& Values() const { return values_; }
  bool Negated() const { return negated_; }

 private:
  std::unordered_set<std::string> values_;
  bool negated_;
};

}  // namespace expassign
