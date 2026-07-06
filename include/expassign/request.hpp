#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "expassign/version.hpp"

namespace expassign {

// Пользовательский запрос: набор именованных свойств разных типов и набор
// идентификаторов пользователя (по одному значению на тип идентификатора,
// например {"uid": "123", "device_id": "abc"}).
//
// Свойства разных типов живут в разных пространствах имён: строковое свойство
// "host" и доменное свойство "host" — это разные свойства.
struct Request {
  std::unordered_map<std::string, std::string> strings;
  std::unordered_map<std::string, bool> bools;
  std::unordered_map<std::string, Version> versions;
  std::unordered_map<std::string, std::string> domains;  // имя -> интернет-хост
  std::unordered_map<std::string, uint32_t> regions;     // имя -> id региона
  // IP пользователя: если явного id региона для свойства нет, регион
  // определяется по IP через диапазоны дерева регионов.
  std::string ip;
  std::unordered_map<std::string, std::string> ids;      // тип id -> значение
};

}  // namespace expassign
