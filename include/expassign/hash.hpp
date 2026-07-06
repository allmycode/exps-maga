#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace expassign {

// Интерфейс хеш-функции для разбиения пользователей.
// По умолчанию используется XXH64; в продакшене сюда легко подключить XXH3
// (обернув xxhash.h) или любую другую 64-битную хеш-функцию — вся логика
// разбиения зависит только от этого интерфейса.
class IHasher {
 public:
  virtual ~IHasher() = default;
  virtual uint64_t Hash(std::string_view data) const = 0;
};

// Самодостаточная реализация XXH64 (без внешних зависимостей).
uint64_t XXH64(const void* data, size_t len, uint64_t seed);

class XXHash64Hasher final : public IHasher {
 public:
  explicit XXHash64Hasher(uint64_t seed = 0) : seed_(seed) {}
  uint64_t Hash(std::string_view data) const override {
    return XXH64(data.data(), data.size(), seed_);
  }

 private:
  uint64_t seed_;
};

}  // namespace expassign
