#include "expassign/indexed_matcher.hpp"

#include <algorithm>
#include <stdexcept>

#include "expassign/bucketing.hpp"
#include "expassign/constraints/bool_constraint.hpp"
#include "expassign/constraints/domain_constraint.hpp"
#include "expassign/constraints/string_constraint.hpp"
#include "expassign/constraints/version_constraint.hpp"

namespace expassign {
namespace {

// Черновой слой на этапе построения: какие эксперименты уже заняли слой и
// какие ограничения в него попали.
template <typename ConstraintT>
struct LayerDraft {
  std::vector<std::pair<uint32_t, const ConstraintT*>> entries;
  std::vector<bool> present;
};

// Раскладывает ограничения по слоям: каждый эксперимент присутствует в слое
// не более одного раза, поэтому несколько ограничений одного эксперимента на
// одно свойство уходят в разные слои.
template <typename ConstraintT>
void AddToLayers(std::vector<LayerDraft<ConstraintT>>& layers, size_t n_exps,
                 uint32_t exp, const ConstraintT* constraint) {
  for (auto& layer : layers) {
    if (!layer.present[exp]) {
      layer.present[exp] = true;
      layer.entries.emplace_back(exp, constraint);
      return;
    }
  }
  auto& layer = layers.emplace_back();
  layer.present.assign(n_exps, false);
  layer.present[exp] = true;
  layer.entries.emplace_back(exp, constraint);
}

template <typename ConstraintT>
DynamicBitset UnconstrainedBits(const LayerDraft<ConstraintT>& draft, size_t n) {
  DynamicBitset bits(n);
  bits.SetAll();
  for (const auto& [exp, _] : draft.entries) bits.Reset(exp);
  return bits;
}

}  // namespace

// --- StringLayer -----------------------------------------------------------

void IndexedMatcher::StringLayer::Filter(const Request& request,
                                         DynamicBitset& out) const {
  auto it = request.strings.find(property);
  out.CopyFrom(unconstrained);
  if (it == request.strings.end()) return;  // все ограниченные — мимо
  out |= negated;
  if (auto n = not_in_lists.find(it->second); n != not_in_lists.end()) {
    for (uint32_t exp : n->second) out.Reset(exp);
  }
  if (auto p = in_lists.find(it->second); p != in_lists.end()) {
    for (uint32_t exp : p->second) out.Set(exp);
  }
}

// --- BoolLayer --------------------------------------------------------------

void IndexedMatcher::BoolLayer::Filter(const Request& request,
                                       DynamicBitset& out) const {
  auto it = request.bools.find(property);
  if (it == request.bools.end()) {
    out.CopyFrom(unconstrained);
    return;
  }
  out.CopyFrom(it->second ? when_true : when_false);
}

// --- VersionLayer -----------------------------------------------------------

void IndexedMatcher::VersionLayer::Filter(const Request& request,
                                          DynamicBitset& out) const {
  auto it = request.versions.find(property);
  if (it == request.versions.end()) {
    out.CopyFrom(unconstrained);
    return;
  }
  const Version& v = it->second;
  auto lb = std::lower_bound(points.begin(), points.end(), v);
  size_t region;
  if (lb != points.end() && *lb == v) {
    region = 2 * static_cast<size_t>(lb - points.begin()) + 1;
  } else {
    region = 2 * static_cast<size_t>(lb - points.begin());
  }
  out.CopyFrom(regions[region]);
}

// --- DomainLayer -------------------------------------------------------------

uint32_t IndexedMatcher::DomainLayer::AddChild(uint32_t parent,
                                               const std::string& label) {
  auto it = nodes[parent].children.find(label);
  if (it != nodes[parent].children.end()) return it->second;
  uint32_t id = static_cast<uint32_t>(nodes.size());
  nodes.emplace_back();
  nodes[parent].children.emplace(label, id);
  return id;
}

uint32_t IndexedMatcher::DomainLayer::AddTldChild(uint32_t parent) {
  if (nodes[parent].tld_child >= 0) {
    return static_cast<uint32_t>(nodes[parent].tld_child);
  }
  uint32_t id = static_cast<uint32_t>(nodes.size());
  nodes.emplace_back();
  nodes[parent].tld_child = static_cast<int32_t>(id);
  return id;
}

void IndexedMatcher::DomainLayer::Filter(const Request& request,
                                         DynamicBitset& out) const {
  out.CopyFrom(unconstrained);
  auto it = request.domains.find(property);
  if (it == request.domains.end()) return;
  std::vector<std::string> labels =
      DomainConstraint::SplitHostReversed(it->second);
  if (labels.empty()) return;

  // От корня возможны две ветки — точная метка TLD и ребро '{tld}'; дальше
  // пути идут только по точным меткам, так что активных узлов не более двух.
  uint32_t cur[2];
  size_t n_cur = 0;
  if (auto child = nodes[0].children.find(labels[0]);
      child != nodes[0].children.end()) {
    cur[n_cur++] = child->second;
  }
  if (nodes[0].tld_child >= 0) {
    cur[n_cur++] = static_cast<uint32_t>(nodes[0].tld_child);
  }

  size_t consumed = 1;
  while (n_cur > 0) {
    for (size_t i = 0; i < n_cur; ++i) {
      const Node& node = nodes[cur[i]];
      // '*' допускает ноль и более дополнительных меток слева, поэтому
      // засчитываем его на каждом пройденном узле.
      for (uint32_t exp : node.star_exps) out.Set(exp);
      if (consumed == labels.size()) {
        for (uint32_t exp : node.exact_exps) out.Set(exp);
      }
    }
    if (consumed == labels.size()) break;
    const std::string& label = labels[consumed];
    size_t next_n = 0;
    uint32_t next[2];
    for (size_t i = 0; i < n_cur; ++i) {
      auto child = nodes[cur[i]].children.find(label);
      if (child != nodes[cur[i]].children.end()) next[next_n++] = child->second;
    }
    std::copy(next, next + next_n, cur);
    n_cur = next_n;
    ++consumed;
  }
}

// --- IndexedMatcher -----------------------------------------------------------

IndexedMatcher::IndexedMatcher(std::vector<FlatExperiment> experiments,
                               std::shared_ptr<const IHasher> hasher)
    : experiments_(std::move(experiments)), hasher_(std::move(hasher)) {
  BuildIndexes();
}

uint32_t IndexedMatcher::InternHashKey(const std::string& id_key,
                                       const std::string& salt) {
  std::string key = id_key + '\x1f' + salt;
  auto [it, inserted] =
      hash_key_index_.emplace(std::move(key), hash_keys_.size());
  if (inserted) hash_keys_.push_back(HashKey{id_key, salt});
  return it->second;
}

void IndexedMatcher::BuildIndexes() {
  const size_t n = experiments_.size();

  std::unordered_map<std::string, std::vector<LayerDraft<StringConstraint>>>
      string_drafts;
  std::unordered_map<std::string, std::vector<LayerDraft<BoolConstraint>>>
      bool_drafts;
  std::unordered_map<std::string, std::vector<LayerDraft<VersionConstraint>>>
      version_drafts;
  std::unordered_map<std::string, std::vector<LayerDraft<DomainConstraint>>>
      domain_drafts;

  hash_refs_.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    const FlatExperiment& exp = experiments_[i];
    if (exp.has_dimension) {
      hash_refs_[i].dim_hash = InternHashKey(exp.dim_id_key, exp.dim_salt);
    }
    hash_refs_[i].exp_hash = InternHashKey(exp.id_key, exp.salt);

    for (const ConstraintPtr& c : exp.constraints) {
      switch (c->Type()) {
        case PropertyType::kString:
          AddToLayers(string_drafts[c->Property()], n, i,
                      static_cast<const StringConstraint*>(c.get()));
          break;
        case PropertyType::kBool:
          AddToLayers(bool_drafts[c->Property()], n, i,
                      static_cast<const BoolConstraint*>(c.get()));
          break;
        case PropertyType::kVersion:
          AddToLayers(version_drafts[c->Property()], n, i,
                      static_cast<const VersionConstraint*>(c.get()));
          break;
        case PropertyType::kDomain:
          AddToLayers(domain_drafts[c->Property()], n, i,
                      static_cast<const DomainConstraint*>(c.get()));
          break;
      }
    }
  }

  for (auto& [property, drafts] : string_drafts) {
    for (auto& draft : drafts) {
      StringLayer layer;
      layer.property = property;
      layer.unconstrained = UnconstrainedBits(draft, n);
      layer.negated = DynamicBitset(n);
      for (const auto& [exp, c] : draft.entries) {
        auto& lists = c->Negated() ? layer.not_in_lists : layer.in_lists;
        if (c->Negated()) layer.negated.Set(exp);
        for (const std::string& value : c->Values()) {
          lists[value].push_back(exp);
        }
      }
      string_layers_.push_back(std::move(layer));
    }
  }

  for (auto& [property, drafts] : bool_drafts) {
    for (auto& draft : drafts) {
      BoolLayer layer;
      layer.property = property;
      layer.unconstrained = UnconstrainedBits(draft, n);
      layer.when_true = layer.unconstrained;
      layer.when_false = layer.unconstrained;
      for (const auto& [exp, c] : draft.entries) {
        (c->Expected() ? layer.when_true : layer.when_false).Set(exp);
      }
      bool_layers_.push_back(std::move(layer));
    }
  }

  for (auto& [property, drafts] : version_drafts) {
    for (auto& draft : drafts) {
      VersionLayer layer;
      layer.property = property;
      layer.unconstrained = UnconstrainedBits(draft, n);

      for (const auto& [exp, c] : draft.entries) {
        for (const VersionInterval& iv : c->Intervals()) {
          if (iv.lo) layer.points.push_back(*iv.lo);
          if (iv.hi) layer.points.push_back(*iv.hi);
        }
      }
      std::sort(layer.points.begin(), layer.points.end());
      layer.points.erase(
          std::unique(layer.points.begin(), layer.points.end()),
          layer.points.end());

      auto point_index = [&](const Version& v) {
        return static_cast<size_t>(
            std::lower_bound(layer.points.begin(), layer.points.end(), v) -
            layer.points.begin());
      };

      size_t n_regions = 2 * layer.points.size() + 1;
      layer.regions.assign(n_regions, layer.unconstrained);
      for (const auto& [exp, c] : draft.entries) {
        for (const VersionInterval& iv : c->Intervals()) {
          size_t first = 0;
          if (iv.lo) {
            size_t i = point_index(*iv.lo);
            first = iv.lo_inclusive ? 2 * i + 1 : 2 * i + 2;
          }
          size_t last = n_regions - 1;
          if (iv.hi) {
            size_t j = point_index(*iv.hi);
            last = iv.hi_inclusive ? 2 * j + 1 : 2 * j;
          }
          for (size_t r = first; r <= last && r < n_regions; ++r) {
            layer.regions[r].Set(exp);
          }
        }
      }
      version_layers_.push_back(std::move(layer));
    }
  }

  for (auto& [property, drafts] : domain_drafts) {
    for (auto& draft : drafts) {
      DomainLayer layer;
      layer.property = property;
      layer.unconstrained = UnconstrainedBits(draft, n);
      layer.nodes.emplace_back();  // корень
      for (const auto& [exp, c] : draft.entries) {
        for (const DomainConstraint::Pattern& p : c->Patterns()) {
          uint32_t cur = 0;
          if (p.tld_wildcard) cur = layer.AddTldChild(cur);
          for (const std::string& label : p.suffix) {
            cur = layer.AddChild(cur, label);
          }
          auto& list =
              p.star ? layer.nodes[cur].star_exps : layer.nodes[cur].exact_exps;
          if (list.empty() || list.back() != exp) list.push_back(exp);
        }
      }
      domain_layers_.push_back(std::move(layer));
    }
  }
}

std::vector<Assignment> IndexedMatcher::Match(const Request& request) const {
  const size_t n = experiments_.size();
  std::vector<Assignment> result;
  if (n == 0) return result;

  DynamicBitset acc(n);
  acc.SetAll();
  DynamicBitset scratch(n);

  auto apply = [&](const auto& layers) {
    for (const auto& layer : layers) {
      layer.Filter(request, scratch);
      acc &= scratch;
      if (!acc.Any()) return false;
    }
    return true;
  };
  if (!apply(string_layers_) || !apply(bool_layers_) ||
      !apply(version_layers_) || !apply(domain_layers_)) {
    return result;
  }

  // Кеш хешей на запрос: эксперименты одного измерения (и эксперименты с
  // одинаковой солью) переиспользуют вычисленный хеш.
  enum class CacheState : uint8_t { kUnknown, kMissingId, kReady };
  struct CacheEntry {
    CacheState state = CacheState::kUnknown;
    uint64_t hash = 0;
  };
  std::vector<CacheEntry> cache(hash_keys_.size());
  auto salted_hash = [&](uint32_t key_index) -> const CacheEntry& {
    CacheEntry& entry = cache[key_index];
    if (entry.state == CacheState::kUnknown) {
      const HashKey& key = hash_keys_[key_index];
      auto it = request.ids.find(key.id_key);
      if (it == request.ids.end()) {
        entry.state = CacheState::kMissingId;
      } else {
        entry.state = CacheState::kReady;
        entry.hash = SaltedHash(*hasher_, it->second, key.salt);
      }
    }
    return entry;
  };

  acc.ForEachSet([&](size_t i) {
    const FlatExperiment& exp = experiments_[i];
    if (exp.has_dimension) {
      const CacheEntry& dim = salted_hash(hash_refs_[i].dim_hash);
      if (dim.state != CacheState::kReady || !SlotAllowed(exp, dim.hash)) return;
    }
    const CacheEntry& own = salted_hash(hash_refs_[i].exp_hash);
    if (own.state != CacheState::kReady) return;
    auto group = GroupForBucket(exp, own.hash);
    if (group) result.push_back(Assignment{&experiments_[i], *group});
  });
  return result;
}

}  // namespace expassign
