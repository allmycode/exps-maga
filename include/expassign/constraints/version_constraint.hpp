#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "expassign/constraints/constraint.hpp"
#include "expassign/version.hpp"

namespace expassign {

// Интервал версий. Отсутствующая граница означает -inf / +inf.
struct VersionInterval {
  std::optional<Version> lo;
  std::optional<Version> hi;
  bool lo_inclusive = true;
  bool hi_inclusive = true;

  bool Contains(const Version& v) const;
};

// Ограничение на свойство-версию: версия из запроса должна попасть хотя бы в
// один из интервалов.
//
// Текстовый формат: интервалы через ';', каждый в виде "[1.0,2.0)", "(2.5,]",
// "[,3.0.1]". Скобка '['/']' — включающая граница, '('/')' — исключающая,
// пустая граница — бесконечность.
class VersionConstraint final : public IConstraint {
 public:
  VersionConstraint(std::string property, std::vector<VersionInterval> intervals);

  // Бросает std::invalid_argument при синтаксической ошибке.
  static std::shared_ptr<const VersionConstraint> Parse(std::string property,
                                                        std::string_view spec);
  static std::vector<VersionInterval> ParseIntervals(std::string_view spec);

  PropertyType Type() const override { return PropertyType::kVersion; }
  bool Matches(const Request& request) const override;

  const std::vector<VersionInterval>& Intervals() const { return intervals_; }

 private:
  std::vector<VersionInterval> intervals_;
};

}  // namespace expassign
