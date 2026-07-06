#pragma once

#include <memory>
#include <string>

namespace expassign {

struct Request;

enum class PropertyType {
  kString,
  kBool,
  kVersion,
  kDomain,
};

// Базовый интерфейс ограничения. Ограничение привязано к одному именованному
// свойству запроса и умеет проверять запрос целиком: если свойство в запросе
// отсутствует, ограничение считается НЕ выполненным.
class IConstraint {
 public:
  virtual ~IConstraint() = default;

  const std::string& Property() const { return property_; }
  virtual PropertyType Type() const = 0;
  virtual bool Matches(const Request& request) const = 0;

 protected:
  explicit IConstraint(std::string property) : property_(std::move(property)) {}

 private:
  std::string property_;
};

// Ограничения разделяются между конфигурацией и уплощёнными экспериментами,
// поэтому хранятся по shared_ptr на неизменяемый объект.
using ConstraintPtr = std::shared_ptr<const IConstraint>;

}  // namespace expassign
