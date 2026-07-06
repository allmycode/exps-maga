#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "expassign/constraints/constraint.hpp"

namespace expassign {

// Ограничение на доменное свойство (интернет-хост).
//
// Задаётся набором шаблонов вида "*.example.{tld}", "mail.example.ru",
// "example.{tld}":
//   * '*' допустим только как самая левая метка целиком и означает любое
//     количество дополнительных поддоменов слева, включая ноль (то есть
//     "*.example.ru" покрывает и "example.ru", и "a.b.example.ru");
//   * '{tld}' допустим только как самая правая метка и означает любой один
//     домен верхнего уровня ("ru", "com", ...);
//   * хотя бы одна метка шаблона должна быть фиксированной.
// Сопоставление регистронезависимое; одна завершающая точка хоста (FQDN)
// игнорируется. Ограничение выполнено, если хост подошёл хотя бы под один
// шаблон.
class DomainConstraint final : public IConstraint {
 public:
  struct Pattern {
    // Фиксированные метки от TLD к младшим (без '*' и без '{tld}').
    std::vector<std::string> suffix;
    bool tld_wildcard = false;  // самая правая метка — {tld}
    bool star = false;          // слева был '*'
  };

  // Бросает std::invalid_argument при некорректном шаблоне.
  DomainConstraint(std::string property, const std::vector<std::string>& patterns);

  PropertyType Type() const override { return PropertyType::kDomain; }
  bool Matches(const Request& request) const override;

  const std::vector<Pattern>& Patterns() const { return patterns_; }

  static Pattern ParsePattern(std::string_view text);
  // Метки хоста в нижнем регистре, от TLD к младшим. Пустой вектор — хост
  // некорректен (пустые метки) и не матчится ни одним шаблоном.
  static std::vector<std::string> SplitHostReversed(std::string_view host);
  static bool MatchHost(const Pattern& pattern,
                        const std::vector<std::string>& host_labels_reversed);

 private:
  std::vector<Pattern> patterns_;
};

}  // namespace expassign
