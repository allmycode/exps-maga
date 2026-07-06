#pragma once

// Внутренний минимальный JSON-парсер — ровно столько, сколько нужно для
// конфигов экспериментов. Не входит в публичный API библиотеки.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace expassign::json {

class Value {
 public:
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  Type type = Type::kNull;
  bool boolean = false;
  double number = 0;
  std::string str;
  std::vector<Value> array;
  std::vector<std::pair<std::string, Value>> object;  // порядок сохраняется

  bool Is(Type t) const { return type == t; }
  // Для объектов: значение по ключу или nullptr.
  const Value* Find(std::string_view key) const;
};

// Бросает std::invalid_argument с позицией ошибки.
Value Parse(std::string_view text);

}  // namespace expassign::json
