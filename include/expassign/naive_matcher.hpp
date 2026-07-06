#pragma once

#include <memory>
#include <vector>

#include "expassign/hash.hpp"
#include "expassign/matcher.hpp"

namespace expassign {

// Эталонная реализация: линейный обход всех экспериментов с проверкой каждого
// ограничения «в лоб». Используется как референс для проверки корректности
// оптимизированного матчера.
class NaiveMatcher final : public IMatcher {
 public:
  NaiveMatcher(std::vector<FlatExperiment> experiments,
               std::shared_ptr<const HasherRegistry> hashers);

  std::vector<Assignment> Match(const Request& request) const override;

  const std::vector<FlatExperiment>& Experiments() const { return experiments_; }

 private:
  std::vector<FlatExperiment> experiments_;
  std::shared_ptr<const HasherRegistry> hashers_;
};

}  // namespace expassign
