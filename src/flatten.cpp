#include "expassign/flatten.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace expassign {
namespace {

void ValidateBucketing(const ExperimentConfig& exp) {
  if (exp.id.empty()) throw std::invalid_argument("experiment with empty id");
  if (exp.id_key.empty()) {
    throw std::invalid_argument("experiment '" + exp.id + "': empty id_key");
  }
  if (exp.total_buckets == 0) {
    throw std::invalid_argument("experiment '" + exp.id + "': total_buckets == 0");
  }
  uint64_t sum = 0;
  for (const auto& g : exp.groups) sum += g.buckets;
  if (sum > exp.total_buckets) {
    throw std::invalid_argument("experiment '" + exp.id +
                                "': groups exceed total_buckets");
  }
}

FlatExperiment FlattenOne(const ExperimentConfig& exp, const DimensionConfig* dim) {
  ValidateBucketing(exp);

  FlatExperiment f;
  f.id = exp.id;
  std::vector<ConstraintPtr> combined;
  if (dim) {
    f.dimension_id = dim->id;
    SlotCheck check;
    check.id_key = dim->id_key;
    check.salt = dim->salt;
    check.total_slots = dim->total_slots;
    check.slots = exp.slots;
    std::sort(check.slots.begin(), check.slots.end());
    if (check.slots.empty()) {
      throw std::invalid_argument("experiment '" + exp.id + "' in dimension '" +
                                  dim->id + "' occupies no slots");
    }
    for (uint32_t s : check.slots) {
      if (s >= dim->total_slots) {
        throw std::invalid_argument("experiment '" + exp.id + "': slot " +
                                    std::to_string(s) + " >= total_slots of '" +
                                    dim->id + "'");
      }
    }
    f.slot_checks.push_back(std::move(check));
    combined = dim->constraints;
    combined.insert(combined.end(), exp.constraints.begin(),
                    exp.constraints.end());
  } else {
    combined = exp.constraints;
  }
  if (!combined.empty()) f.restrictions.push_back(std::move(combined));

  f.id_key = exp.id_key;
  f.salt = exp.salt;
  f.total_buckets = exp.total_buckets;
  uint32_t cursor = 0;
  for (const auto& g : exp.groups) {
    GroupRange range;
    range.name = g.name;
    range.begin = cursor;
    range.end = cursor + g.buckets;
    cursor = range.end;
    f.groups.push_back(std::move(range));
  }
  return f;
}

}  // namespace

std::vector<FlatExperiment> Flatten(const Config& config) {
  std::vector<FlatExperiment> out;
  for (const auto& dim : config.dimensions) {
    if (dim.id.empty()) throw std::invalid_argument("dimension with empty id");
    if (dim.id_key.empty()) {
      throw std::invalid_argument("dimension '" + dim.id + "': empty id_key");
    }
    if (dim.total_slots == 0) {
      throw std::invalid_argument("dimension '" + dim.id + "': total_slots == 0");
    }
    std::unordered_set<uint32_t> used;
    for (const auto& exp : dim.experiments) {
      for (uint32_t s : exp.slots) {
        if (!used.insert(s).second) {
          throw std::invalid_argument("dimension '" + dim.id + "': slot " +
                                      std::to_string(s) +
                                      " is occupied by more than one experiment");
        }
      }
      out.push_back(FlattenOne(exp, &dim));
    }
  }
  for (const auto& exp : config.experiments) {
    out.push_back(FlattenOne(exp, nullptr));
  }
  return out;
}

}  // namespace expassign
