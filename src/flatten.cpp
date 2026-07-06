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
  if (dim) {
    f.has_dimension = true;
    f.dimension_id = dim->id;
    f.dim_id_key = dim->id_key;
    f.dim_salt = dim->salt;
    f.dim_total_slots = dim->total_slots;
    f.slots = exp.slots;
    std::sort(f.slots.begin(), f.slots.end());
    f.constraints = dim->constraints;
    f.constraints.insert(f.constraints.end(), exp.constraints.begin(),
                         exp.constraints.end());
    if (f.slots.empty()) {
      throw std::invalid_argument("experiment '" + exp.id + "' in dimension '" +
                                  dim->id + "' occupies no slots");
    }
    for (uint32_t s : f.slots) {
      if (s >= dim->total_slots) {
        throw std::invalid_argument("experiment '" + exp.id + "': slot " +
                                    std::to_string(s) + " >= total_slots of '" +
                                    dim->id + "'");
      }
    }
  } else {
    f.constraints = exp.constraints;
  }
  f.id_key = exp.id_key;
  f.salt = exp.salt;
  f.total_buckets = exp.total_buckets;
  f.groups = exp.groups;
  uint32_t end = 0;
  for (const auto& g : exp.groups) {
    end += g.buckets;
    f.group_ends.push_back(end);
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
