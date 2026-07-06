#include "expassign/naive_matcher.hpp"

#include "expassign/bucketing.hpp"

namespace expassign {

NaiveMatcher::NaiveMatcher(std::vector<FlatExperiment> experiments,
                           std::shared_ptr<const HasherRegistry> hashers)
    : experiments_(std::move(experiments)), hashers_(std::move(hashers)) {
  ValidateHashers(experiments_, *hashers_);
}

std::vector<Assignment> NaiveMatcher::Match(const Request& request) const {
  std::vector<Assignment> result;
  for (const FlatExperiment& exp : experiments_) {
    if (!RestrictionsMatch(exp.restrictions, request)) continue;

    bool slots_ok = true;
    for (const SlotCheck& check : exp.slot_checks) {
      auto id_it = request.ids.find(check.id_key);
      if (id_it == request.ids.end()) {
        slots_ok = false;
        break;
      }
      const IHasher* hasher = hashers_->Get(check.hash_algo);
      if (!SlotAllowed(check, SaltedHash(*hasher, id_it->second, check.salt))) {
        slots_ok = false;
        break;
      }
    }
    if (!slots_ok) continue;

    auto id_it = request.ids.find(exp.id_key);
    if (id_it == request.ids.end()) continue;
    const IHasher* hasher = hashers_->Get(exp.hash_algo);
    auto group =
        GroupForBucket(exp, SaltedHash(*hasher, id_it->second, exp.salt));
    if (!group) continue;
    result.push_back(Assignment{&exp, *group,
                                MatchedSections(exp.groups[*group], request)});
  }
  return result;
}

}  // namespace expassign
