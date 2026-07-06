#include "expassign/naive_matcher.hpp"

#include "expassign/bucketing.hpp"

namespace expassign {

NaiveMatcher::NaiveMatcher(std::vector<FlatExperiment> experiments,
                           std::shared_ptr<const IHasher> hasher)
    : experiments_(std::move(experiments)), hasher_(std::move(hasher)) {}

std::vector<Assignment> NaiveMatcher::Match(const Request& request) const {
  std::vector<Assignment> result;
  for (const FlatExperiment& exp : experiments_) {
    bool ok = true;
    for (const ConstraintPtr& c : exp.constraints) {
      if (!c->Matches(request)) {
        ok = false;
        break;
      }
    }
    if (!ok) continue;

    if (exp.has_dimension) {
      auto id_it = request.ids.find(exp.dim_id_key);
      if (id_it == request.ids.end()) continue;
      if (!SlotAllowed(exp, SaltedHash(*hasher_, id_it->second, exp.dim_salt))) {
        continue;
      }
    }

    auto id_it = request.ids.find(exp.id_key);
    if (id_it == request.ids.end()) continue;
    auto group =
        GroupForBucket(exp, SaltedHash(*hasher_, id_it->second, exp.salt));
    if (group) result.push_back(Assignment{&exp, *group});
  }
  return result;
}

}  // namespace expassign
