#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "expassign/experiment.hpp"

namespace expassign {

// Одна проверка слота: hash(ids[id_key] + salt) % total_slots должен попасть
// в slots. В терминах конфига это одно «место» (place) с явными слотами; у
// эксперимента их может быть несколько — цепочка вложенных разбиений.
struct SlotCheck {
  std::string id_key;
  std::string hash_algo;  // "" — хешер по умолчанию из HasherRegistry
  std::string salt;
  uint32_t total_slots = 0;
  std::vector<uint32_t> slots;  // отсортированы, уникальны
};

// Секция группы: полезная нагрузка (params) с необязательными собственными
// ограничениями — OR-список AND-групп, пустой список означает «всегда».
struct Section {
  std::vector<std::vector<ConstraintPtr>> restrictions;
  std::string params;
};

// Группа экспериментальных значений (testid): занимает диапазон бакетов
// [begin, end) финального разбиения. Диапазоны групп могут идти с дырами —
// бакеты вне всех групп не дают назначения.
struct GroupRange {
  std::string name;  // testid
  uint32_t begin = 0;
  uint32_t end = 0;
  std::vector<Section> sections;
};

// Уплощённый эксперимент: вся информация измерений (ограничения, соли,
// слоты) перенесена внутрь, матчеры работают с плоским списком.
struct FlatExperiment {
  std::string id;
  std::string type;          // например "ABT"; информационное поле
  std::string dimension_id;  // для программного API; может быть пустым

  // Ограничения: OR-список AND-групп (вариантов). Пустой список — без
  // ограничений. Эксперимент подходит, если выполнены все ограничения хотя
  // бы одного варианта.
  std::vector<std::vector<ConstraintPtr>> restrictions;

  // Цепочка проверок слотов (все места, кроме финального разбиения).
  std::vector<SlotCheck> slot_checks;

  // Финальное разбиение на группы.
  std::string id_key;
  std::string hash_algo;  // "" — хешер по умолчанию
  std::string salt;
  uint32_t total_buckets = 0;
  std::vector<GroupRange> groups;  // отсортированы по begin, без пересечений
};

// Валидирует конфигурацию и уплощает её. Бросает std::invalid_argument при
// некорректной конфигурации (пустые id, пересекающиеся слоты, переполнение
// бакетов и т.п.).
std::vector<FlatExperiment> Flatten(const Config& config);

}  // namespace expassign
