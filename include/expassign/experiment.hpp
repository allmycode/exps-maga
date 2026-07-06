#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "expassign/constraints/constraint.hpp"

namespace expassign {

// Экспериментальная группа: имя и размер в бакетах разбиения.
struct GroupSpec {
  std::string name;
  uint32_t buckets = 0;
};

// Конфигурация эксперимента.
//
// Разбиение на группы: hash(ids[id_key] + salt) % total_buckets даёт бакет;
// группы занимают последовательные диапазоны бакетов в порядке объявления.
// Если сумма размеров групп меньше total_buckets, остаток бакетов ни в одну
// группу не попадает (holdout).
//
// Если эксперимент лежит в измерении, slots — занимаемые им слоты измерения.
struct ExperimentConfig {
  std::string id;
  std::vector<ConstraintPtr> constraints;
  std::string id_key;  // какой идентификатор пользователя использовать
  std::string salt;
  uint32_t total_buckets = 100;
  std::vector<GroupSpec> groups;
  std::vector<uint32_t> slots;  // используется только внутри измерения
};

// Измерение — «эксперимент более высокого порядка»: собственный набор
// ограничений и собственное разбиение. hash(ids[id_key] + salt) % total_slots
// даёт слот; запрос может попасть только в тот эксперимент измерения, которому
// принадлежит этот слот. Слоты экспериментов одного измерения не пересекаются,
// поэтому эксперименты внутри измерения взаимоисключающи, а эксперименты из
// разных измерений могут пересекаться между собой.
struct DimensionConfig {
  std::string id;
  std::vector<ConstraintPtr> constraints;
  std::string id_key;
  std::string salt;
  uint32_t total_slots = 1;
  std::vector<ExperimentConfig> experiments;
};

// Полная конфигурация: эксперименты в измерениях и свободные эксперименты вне
// измерений (для них проверка слота не выполняется).
struct Config {
  std::vector<DimensionConfig> dimensions;
  std::vector<ExperimentConfig> experiments;
};

}  // namespace expassign
