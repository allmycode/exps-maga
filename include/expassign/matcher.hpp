#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "expassign/flatten.hpp"
#include "expassign/request.hpp"

namespace expassign {

// Результат назначения: эксперимент и выбранная в нём группа.
struct Assignment {
  const FlatExperiment* experiment = nullptr;
  size_t group_index = 0;

  const std::string& ExperimentId() const { return experiment->id; }
  const std::string& GroupName() const {
    return experiment->groups[group_index].name;
  }
};

class IMatcher {
 public:
  virtual ~IMatcher() = default;

  // Возвращает назначения в порядке следования экспериментов в уплощённом
  // списке.
  virtual std::vector<Assignment> Match(const Request& request) const = 0;
};

}  // namespace expassign
