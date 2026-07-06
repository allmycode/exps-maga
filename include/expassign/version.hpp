#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace expassign {

// Версия вида "1.2.3.4": произвольное количество числовых компонентов,
// разделённых точками. Сравнение выравнивает более короткую версию нулями,
// поэтому "1.2" == "1.2.0".
class Version {
 public:
  Version() = default;
  explicit Version(std::vector<uint32_t> parts) : parts_(std::move(parts)) {}

  // Возвращает std::nullopt для некорректной строки (пустая строка, пустые
  // компоненты, нечисловые символы, переполнение компонента).
  static std::optional<Version> Parse(std::string_view text);

  const std::vector<uint32_t>& Parts() const { return parts_; }
  std::string ToString() const;

  // <0, 0, >0 — как strcmp.
  int Compare(const Version& other) const;

  bool operator==(const Version& o) const { return Compare(o) == 0; }
  bool operator!=(const Version& o) const { return Compare(o) != 0; }
  bool operator<(const Version& o) const { return Compare(o) < 0; }
  bool operator<=(const Version& o) const { return Compare(o) <= 0; }
  bool operator>(const Version& o) const { return Compare(o) > 0; }
  bool operator>=(const Version& o) const { return Compare(o) >= 0; }

 private:
  std::vector<uint32_t> parts_;
};

}  // namespace expassign
