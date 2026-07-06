#include "expassign/constraints/region_constraint.hpp"

#include <stdexcept>

#include "expassign/request.hpp"

namespace expassign {

RegionConstraint::RegionConstraint(std::string property,
                                   std::shared_ptr<const RegionTree> tree,
                                   std::vector<RegionTree::RegionId> regions,
                                   bool negated)
    : IConstraint(std::move(property)),
      tree_(std::move(tree)),
      regions_(regions.begin(), regions.end()),
      negated_(negated) {
  if (!tree_) {
    throw std::invalid_argument("region constraint requires a region tree");
  }
  if (regions_.empty()) {
    throw std::invalid_argument("region constraint requires at least one region");
  }
}

std::optional<RegionTree::RegionId> RegionConstraint::Resolve(
    const Request& request, const std::string& property,
    const RegionTree& tree) {
  auto it = request.regions.find(property);
  if (it != request.regions.end()) return it->second;
  if (!request.ip.empty()) return tree.RegionByIp(request.ip);
  return std::nullopt;
}

bool RegionConstraint::Matches(const Request& request) const {
  auto region = Resolve(request, Property(), *tree_);
  if (!region) return false;
  bool inside = false;
  tree_->ForEachAncestor(*region, [&](RegionTree::RegionId ancestor) {
    if (regions_.contains(ancestor)) inside = true;
  });
  return inside != negated_;
}

}  // namespace expassign
