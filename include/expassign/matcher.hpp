#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "expassign/flatten.hpp"
#include "expassign/request.hpp"

namespace expassign {

// Результат назначения: эксперимент, выбранная группа (testid) и индексы
// секций группы, чьи ограничения выполнены для запроса.
struct Assignment {
  const FlatExperiment* experiment = nullptr;
  size_t group_index = 0;
  std::vector<uint32_t> sections;

  const std::string& ExperimentId() const { return experiment->id; }
  const GroupRange& Group() const { return experiment->groups[group_index]; }
  const std::string& GroupName() const { return Group().name; }
};

class IMatcher {
 public:
  virtual ~IMatcher() = default;

  // Возвращает назначения в порядке следования экспериментов в уплощённом
  // списке; не более одного назначения на эксперимент.
  virtual std::vector<Assignment> Match(const Request& request) const = 0;
};

}  // namespace expassign
