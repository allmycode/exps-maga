#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace expassign {

// Дерево регионов (геобаза): регион идентифицируется целым числом, у каждого
// региона, кроме корневого, есть родительский. Дополнительно хранит диапазоны
// IP-адресов, по которым определяется регион запроса, если явного
// идентификатора в запросе нет.
//
// Поддерживаются IPv4 и IPv6; IPv4 отображается в IPv4-mapped IPv6
// (::ffff:a.b.c.d), так что все адреса живут в одном 128-битном пространстве
// и диапазоны обеих семей не конфликтуют.
//
// Использование: AddRegion/AddIpRange, затем обязательный Build() (сортировка
// диапазонов и валидация), после этого структура иммутабельна и безопасна для
// конкурентного чтения.
class RegionTree {
 public:
  using RegionId = uint32_t;
  using IpBytes = std::array<uint8_t, 16>;

  // Корневой регион задаётся parent == id. Повторное добавление того же id —
  // std::invalid_argument.
  void AddRegion(RegionId id, RegionId parent);

  // Диапазон [from, to] включительно. Бросает std::invalid_argument при
  // некорректном IP или from > to.
  void AddIpRange(std::string_view from, std::string_view to, RegionId region);

  // Сортирует диапазоны и валидирует структуру: пересекающиеся диапазоны и
  // циклы в дереве — std::invalid_argument.
  void Build();
  bool Built() const { return built_; }

  std::optional<RegionId> Parent(RegionId id) const;

  // Обходит цепочку предков, начиная с самого региона. Неизвестный дереву
  // регион даёт цепочку из него одного.
  template <typename F>
  void ForEachAncestor(RegionId region, F&& f) const {
    RegionId cur = region;
    for (size_t guard = 0; guard <= parents_.size(); ++guard) {
      f(cur);
      auto it = parents_.find(cur);
      if (it == parents_.end() || it->second == cur) return;
      cur = it->second;
    }
  }

  // region совпадает с ancestor или лежит внутри него.
  bool Contains(RegionId ancestor, RegionId region) const;

  // Регион по IP-адресу (строка IPv4/IPv6). std::nullopt — IP некорректен или
  // не попал ни в один диапазон. Требует Build() (иначе std::logic_error).
  std::optional<RegionId> RegionByIp(std::string_view ip) const;

  static std::optional<IpBytes> ParseIp(std::string_view ip);

 private:
  struct IpRange {
    IpBytes from;
    IpBytes to;
    RegionId region;
  };

  std::unordered_map<RegionId, RegionId> parents_;
  std::vector<IpRange> ranges_;
  bool built_ = false;
};

}  // namespace expassign
