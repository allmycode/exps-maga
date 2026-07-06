#pragma once

#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "expassign/constraints/constraint.hpp"
#include "expassign/flatten.hpp"
#include "expassign/region_tree.hpp"

namespace expassign {

// Схема разбора блока restrictions: тип ограничения по ключу.
struct ConfigSchema {
  // Явное соответствие «ключ -> тип». Для ключей вне карты действует
  // эвристика: имена, оканчивающиеся на "_versions", считаются версионными,
  // остальные — строковыми.
  std::unordered_map<std::string, PropertyType> key_types;

  // Дерево регионов; обязательно, если конфиг содержит региональные ключи.
  std::shared_ptr<const RegionTree> region_tree;

  // По умолчанию: {"regions" -> kRegion}.
  static ConfigSchema Default();
};

// Загружает JSON-конфиг экспериментов (массив записей вида
// {restrictions, places, testids, type}) в плоский список экспериментов:
//
//   * restrictions — массив блоков: OR между блоками, AND между ключами
//     блока; значения — строки со списками через запятую; версии — интервалы
//     вида "[2025.12.3;+inf)" (границы через ';', +inf/-inf/пусто —
//     бесконечность), регионы — списки числовых id;
//   * places — цепочка разбиений: все места, кроме последнего, должны иметь
//     "slots" и становятся проверками слотов; последнее место задаёт
//     разбиение на группы (его size — число бакетов);
//   * salt вида "XXH3:abcd" — алгоритм до ':' и соль после; соль без
//     префикса использует алгоритм по умолчанию ("" в HasherRegistry);
//   * id места — тип идентификатора пользователя; число N превращается в
//     ключ "N" карты Request::ids;
//   * testids — группы: slot задаёт бакет последнего места ([slot, slot+1)),
//     sections с params и необязательными собственными restrictions
//     (объект или массив блоков) попадают в GroupRange::sections;
//   * поля percent и slots_count проверяются на согласованность со slots,
//     где это возможно, но на матчинг не влияют.
//
// Слоты мест, разделяющих одну тройку (id, алгоритм+соль, size), проверяются
// на пересечение между записями — это общее «измерение».
//
// Бросает std::invalid_argument при ошибках формата.
std::vector<FlatExperiment> LoadExperimentsJson(std::string_view json_text,
                                                const ConfigSchema& schema);

}  // namespace expassign
