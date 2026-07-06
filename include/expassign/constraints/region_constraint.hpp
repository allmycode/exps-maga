#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "expassign/constraints/constraint.hpp"
#include "expassign/region_tree.hpp"

namespace expassign {

// Ограничение по регионам. Задаётся списком регионов, в один из которых регион
// запроса должен входить (negated == false) либо, наоборот, не входить
// (negated == true). Вхождение проверяется по дереву регионов: регион входит
// в заданный, если совпадает с ним или лежит внутри него по цепочке предков.
//
// Регион запроса определяется так: явный идентификатор из
// request.regions[property], а если его нет — по request.ip через диапазоны
// того же дерева. Если регион не удалось определить, ограничение не выполнено
// в обоих режимах. Регион, неизвестный дереву, считается корнем собственной
// пустой цепочки: он входит только сам в себя.
class RegionConstraint final : public IConstraint {
 public:
  RegionConstraint(std::string property, std::shared_ptr<const RegionTree> tree,
                   std::vector<RegionTree::RegionId> regions,
                   bool negated = false);

  PropertyType Type() const override { return PropertyType::kRegion; }
  bool Matches(const Request& request) const override;

  const std::shared_ptr<const RegionTree>& Tree() const { return tree_; }
  const std::unordered_set<RegionTree::RegionId>& Regions() const {
    return regions_;
  }
  bool Negated() const { return negated_; }

  // Общая для обоих матчеров логика определения региона запроса.
  static std::optional<RegionTree::RegionId> Resolve(const Request& request,
                                                     const std::string& property,
                                                     const RegionTree& tree);

 private:
  std::shared_ptr<const RegionTree> tree_;
  std::unordered_set<RegionTree::RegionId> regions_;
  bool negated_;
};

}  // namespace expassign
