#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "expassign/flatten.hpp"
#include "expassign/hash.hpp"
#include "expassign/request.hpp"

namespace expassign {

// hash(идентификатор + соль) — общая точка для слотов и бакетов.
inline uint64_t SaltedHash(const IHasher& hasher, std::string_view id_value,
                           std::string_view salt) {
  std::string buf;
  buf.reserve(id_value.size() + salt.size());
  buf.append(id_value);
  buf.append(salt);
  return hasher.Hash(buf);
}

// Попадает ли хеш в один из слотов проверки.
inline bool SlotAllowed(const SlotCheck& check, uint64_t hash) {
  uint32_t slot = static_cast<uint32_t>(hash % check.total_slots);
  return std::binary_search(check.slots.begin(), check.slots.end(), slot);
}

// Индекс группы для хеша финального разбиения; std::nullopt — бакет вне всех
// групп (дыра или holdout).
inline std::optional<size_t> GroupForBucket(const FlatExperiment& exp,
                                            uint64_t hash) {
  uint32_t bucket = static_cast<uint32_t>(hash % exp.total_buckets);
  auto it = std::upper_bound(
      exp.groups.begin(), exp.groups.end(), bucket,
      [](uint32_t b, const GroupRange& g) { return b < g.begin; });
  if (it == exp.groups.begin()) return std::nullopt;
  --it;
  if (bucket >= it->end) return std::nullopt;
  return static_cast<size_t>(it - exp.groups.begin());
}

// OR-список AND-групп: пустой список — выполнено, иначе должен полностью
// выполниться хотя бы один вариант.
inline bool RestrictionsMatch(
    const std::vector<std::vector<ConstraintPtr>>& variants,
    const Request& request) {
  if (variants.empty()) return true;
  for (const auto& variant : variants) {
    bool ok = true;
    for (const ConstraintPtr& c : variant) {
      if (!c->Matches(request)) {
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

// Индексы секций группы, чьи ограничения выполнены для запроса.
inline std::vector<uint32_t> MatchedSections(const GroupRange& group,
                                             const Request& request) {
  std::vector<uint32_t> out;
  for (uint32_t i = 0; i < group.sections.size(); ++i) {
    if (RestrictionsMatch(group.sections[i].restrictions, request)) {
      out.push_back(i);
    }
  }
  return out;
}

// Проверка на этапе построения матчера: все алгоритмы хеширования известны
// реестру. Бросает std::invalid_argument.
inline void ValidateHashers(const std::vector<FlatExperiment>& experiments,
                            const HasherRegistry& registry) {
  auto check = [&](const std::string& algo) {
    if (registry.Get(algo) == nullptr) {
      throw std::invalid_argument("unknown hash algorithm '" + algo +
                                  "' (register it in HasherRegistry)");
    }
  };
  for (const FlatExperiment& exp : experiments) {
    for (const SlotCheck& sc : exp.slot_checks) check(sc.hash_algo);
    check(exp.hash_algo);
  }
}

}  // namespace expassign
