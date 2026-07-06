#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "expassign/experiment.hpp"

namespace expassign {

// Уплощённый эксперимент: вся информация измерения (ограничения, соль,
// разбиение по слотам) перенесена внутрь эксперимента, так что матчеры
// работают с плоским списком.
struct FlatExperiment {
  std::string id;

  // Ограничения измерения, затем собственные ограничения эксперимента.
  std::vector<ConstraintPtr> constraints;

  // Проверка слота измерения.
  bool has_dimension = false;
  std::string dimension_id;
  std::string dim_id_key;
  std::string dim_salt;
  uint32_t dim_total_slots = 0;
  std::vector<uint32_t> slots;  // отсортированы

  // Разбиение на группы.
  std::string id_key;
  std::string salt;
  uint32_t total_buckets = 0;
  std::vector<GroupSpec> groups;
  std::vector<uint32_t> group_ends;  // накопленные концы диапазонов бакетов
};

// Валидирует конфигурацию и уплощает её. Бросает std::invalid_argument при
// некорректной конфигурации (пустые id, пересекающиеся слоты, переполнение
// бакетов и т.п.).
std::vector<FlatExperiment> Flatten(const Config& config);

}  // namespace expassign
