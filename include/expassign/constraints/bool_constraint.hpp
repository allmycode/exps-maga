#pragma once

#include <string>

#include "expassign/constraints/constraint.hpp"

namespace expassign {

// Ограничение на булевое свойство: свойство должно присутствовать в запросе и
// иметь ровно заданное значение.
class BoolConstraint final : public IConstraint {
 public:
  BoolConstraint(std::string property, bool expected)
      : IConstraint(std::move(property)), expected_(expected) {}

  PropertyType Type() const override { return PropertyType::kBool; }
  bool Matches(const Request& request) const override;

  bool Expected() const { return expected_; }

 private:
  bool expected_;
};

}  // namespace expassign
