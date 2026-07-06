#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "expassign/flatten.hpp"
#include "expassign/hash.hpp"

namespace expassign {

// hash(идентификатор + соль) — общая точка для слотов измерений и бакетов
// экспериментов.
inline uint64_t SaltedHash(const IHasher& hasher, std::string_view id_value,
                           std::string_view salt) {
  std::string buf;
  buf.reserve(id_value.size() + salt.size());
  buf.append(id_value);
  buf.append(salt);
  return hasher.Hash(buf);
}

// Попадает ли хеш измерения в один из слотов эксперимента.
inline bool SlotAllowed(const FlatExperiment& exp, uint64_t dim_hash) {
  uint32_t slot = static_cast<uint32_t>(dim_hash % exp.dim_total_slots);
  return std::binary_search(exp.slots.begin(), exp.slots.end(), slot);
}

// Индекс группы для хеша эксперимента; std::nullopt — бакет вне всех групп.
inline std::optional<size_t> GroupForBucket(const FlatExperiment& exp,
                                            uint64_t hash) {
  uint32_t bucket = static_cast<uint32_t>(hash % exp.total_buckets);
  auto it = std::upper_bound(exp.group_ends.begin(), exp.group_ends.end(), bucket);
  if (it == exp.group_ends.end()) return std::nullopt;
  return static_cast<size_t>(it - exp.group_ends.begin());
}

}  // namespace expassign
