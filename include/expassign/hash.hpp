#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

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

// Реестр хеш-функций: каждое разбиение (место в измерении, разбиение на
// группы) может указывать алгоритм — в конфиге это префикс соли до ':'
// ("XXH3:abcd" -> алгоритм "XXH3", соль "abcd"; соль без префикса -> алгоритм
// "" — по умолчанию).
class HasherRegistry {
 public:
  void Register(std::string algo, std::shared_ptr<const IHasher> hasher) {
    hashers_[std::move(algo)] = std::move(hasher);
  }

  // nullptr, если алгоритм не зарегистрирован.
  const IHasher* Get(const std::string& algo) const {
    auto it = hashers_.find(algo);
    return it == hashers_.end() ? nullptr : it->second.get();
  }

  // Реестр по умолчанию: "" и "XXH3" отображены на встроенный XXH64.
  // ВНИМАНИЕ: "XXH3" здесь — подстановка, чтобы конфиги загружались из
  // коробки; для совместимости разбиений с продакшеном зарегистрируйте
  // настоящий XXH3 (обёртку над xxhash.h) вместо этой подстановки.
  static std::shared_ptr<const HasherRegistry> CreateDefault() {
    auto registry = std::make_shared<HasherRegistry>();
    auto xxh64 = std::make_shared<XXHash64Hasher>();
    registry->Register("", xxh64);
    registry->Register("XXH3", xxh64);
    return registry;
  }

 private:
  std::unordered_map<std::string, std::shared_ptr<const IHasher>> hashers_;
};

}  // namespace expassign
