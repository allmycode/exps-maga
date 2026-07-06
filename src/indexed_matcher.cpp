#include "expassign/indexed_matcher.hpp"

#include <algorithm>
#include <stdexcept>

#include "expassign/bucketing.hpp"
#include "expassign/constraints/bool_constraint.hpp"
#include "expassign/constraints/domain_constraint.hpp"
#include "expassign/constraints/region_constraint.hpp"
#include "expassign/constraints/string_constraint.hpp"
#include "expassign/constraints/version_constraint.hpp"

namespace expassign {
namespace {

// Черновой слой на этапе построения: какие варианты уже заняли слой и какие
// ограничения в него попали.
template <typename ConstraintT>
struct LayerDraft {
  std::vector<std::pair<uint32_t, const ConstraintT*>> entries;
  std::vector<bool> present;
};

// Раскладывает ограничения по слоям: каждый вариант присутствует в слое не
// более одного раза, поэтому несколько ограничений одного варианта на одно
// свойство уходят в разные слои.
template <typename ConstraintT>
void AddToLayers(std::vector<LayerDraft<ConstraintT>>& layers, size_t n_variants,
                 uint32_t variant, const ConstraintT* constraint) {
  for (auto& layer : layers) {
    if (!layer.present[variant]) {
      layer.present[variant] = true;
      layer.entries.emplace_back(variant, constraint);
      return;
    }
  }
  auto& layer = layers.emplace_back();
  layer.present.assign(n_variants, false);
  layer.present[variant] = true;
  layer.entries.emplace_back(variant, constraint);
}

template <typename ConstraintT>
DynamicBitset UnconstrainedBits(const LayerDraft<ConstraintT>& draft, size_t n) {
  DynamicBitset bits(n);
  bits.SetAll();
  for (const auto& [variant, _] : draft.entries) bits.Reset(variant);
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
    for (uint32_t variant : n->second) out.Reset(variant);
  }
  if (auto p = in_lists.find(it->second); p != in_lists.end()) {
    for (uint32_t variant : p->second) out.Set(variant);
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
      for (uint32_t variant : node.star_vars) out.Set(variant);
      if (consumed == labels.size()) {
        for (uint32_t variant : node.exact_vars) out.Set(variant);
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

// --- RegionLayer --------------------------------------------------------------

void IndexedMatcher::RegionLayer::Filter(const Request& request,
                                         DynamicBitset& out) const {
  out.CopyFrom(unconstrained);
  auto region = RegionConstraint::Resolve(request, property, *tree);
  if (!region) return;  // регион не определён — все ограниченные мимо
  out |= negated;
  // Внутри слоя вариант либо позитивный, либо негативный, поэтому Set и
  // Reset по разным предкам не конфликтуют между собой.
  tree->ForEachAncestor(*region, [&](uint32_t ancestor) {
    if (auto n = not_in_lists.find(ancestor); n != not_in_lists.end()) {
      for (uint32_t variant : n->second) out.Reset(variant);
    }
    if (auto p = in_lists.find(ancestor); p != in_lists.end()) {
      for (uint32_t variant : p->second) out.Set(variant);
    }
  });
}

// --- IndexedMatcher -----------------------------------------------------------

IndexedMatcher::IndexedMatcher(std::vector<FlatExperiment> experiments,
                               std::shared_ptr<const HasherRegistry> hashers)
    : experiments_(std::move(experiments)), hashers_(std::move(hashers)) {
  ValidateHashers(experiments_, *hashers_);
  BuildIndexes();
}

uint32_t IndexedMatcher::InternHashKey(const std::string& id_key,
                                       const std::string& algo,
                                       const std::string& salt) {
  std::string key = id_key + '\x1f' + algo + '\x1f' + salt;
  auto [it, inserted] =
      hash_key_index_.emplace(std::move(key), hash_keys_.size());
  if (inserted) {
    hash_keys_.push_back(HashKey{id_key, salt, hashers_->Get(algo)});
  }
  return it->second;
}

void IndexedMatcher::BuildIndexes() {
  const size_t n = experiments_.size();

  // Проход 1: раскладка вариантов по битам и кеш-ключи хешей. Эксперимент без
  // ограничений получает один вариант без ограничений.
  hash_refs_.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    const FlatExperiment& exp = experiments_[i];
    for (const SlotCheck& check : exp.slot_checks) {
      hash_refs_[i].slot_keys.push_back(
          InternHashKey(check.id_key, check.hash_algo, check.salt));
    }
    hash_refs_[i].bucket_key = InternHashKey(exp.id_key, exp.hash_algo, exp.salt);

    size_t variants = exp.restrictions.empty() ? 1 : exp.restrictions.size();
    for (size_t v = 0; v < variants; ++v) variant_exp_.push_back(i);
  }
  const size_t nv = variant_exp_.size();

  // Проход 2: раскладка ограничений по слоям свойств.
  std::unordered_map<std::string, std::vector<LayerDraft<StringConstraint>>>
      string_drafts;
  std::unordered_map<std::string, std::vector<LayerDraft<BoolConstraint>>>
      bool_drafts;
  std::unordered_map<std::string, std::vector<LayerDraft<VersionConstraint>>>
      version_drafts;
  std::unordered_map<std::string, std::vector<LayerDraft<DomainConstraint>>>
      domain_drafts;
  std::unordered_map<std::string, std::vector<LayerDraft<RegionConstraint>>>
      region_drafts;

  uint32_t variant = 0;
  for (uint32_t i = 0; i < n; ++i) {
    const FlatExperiment& exp = experiments_[i];
    if (exp.restrictions.empty()) {
      ++variant;  // вариант без ограничений: во всех слоях unconstrained
      continue;
    }
    for (const auto& and_group : exp.restrictions) {
      for (const ConstraintPtr& c : and_group) {
        switch (c->Type()) {
          case PropertyType::kString:
            AddToLayers(string_drafts[c->Property()], nv, variant,
                        static_cast<const StringConstraint*>(c.get()));
            break;
          case PropertyType::kBool:
            AddToLayers(bool_drafts[c->Property()], nv, variant,
                        static_cast<const BoolConstraint*>(c.get()));
            break;
          case PropertyType::kVersion:
            AddToLayers(version_drafts[c->Property()], nv, variant,
                        static_cast<const VersionConstraint*>(c.get()));
            break;
          case PropertyType::kDomain:
            AddToLayers(domain_drafts[c->Property()], nv, variant,
                        static_cast<const DomainConstraint*>(c.get()));
            break;
          case PropertyType::kRegion:
            AddToLayers(region_drafts[c->Property()], nv, variant,
                        static_cast<const RegionConstraint*>(c.get()));
            break;
        }
      }
      ++variant;
    }
  }

  for (auto& [property, drafts] : string_drafts) {
    for (auto& draft : drafts) {
      StringLayer layer;
      layer.property = property;
      layer.unconstrained = UnconstrainedBits(draft, nv);
      layer.negated = DynamicBitset(nv);
      for (const auto& [var, c] : draft.entries) {
        auto& lists = c->Negated() ? layer.not_in_lists : layer.in_lists;
        if (c->Negated()) layer.negated.Set(var);
        for (const std::string& value : c->Values()) {
          lists[value].push_back(var);
        }
      }
      string_layers_.push_back(std::move(layer));
    }
  }

  for (auto& [property, drafts] : bool_drafts) {
    for (auto& draft : drafts) {
      BoolLayer layer;
      layer.property = property;
      layer.unconstrained = UnconstrainedBits(draft, nv);
      layer.when_true = layer.unconstrained;
      layer.when_false = layer.unconstrained;
      for (const auto& [var, c] : draft.entries) {
        (c->Expected() ? layer.when_true : layer.when_false).Set(var);
      }
      bool_layers_.push_back(std::move(layer));
    }
  }

  for (auto& [property, drafts] : version_drafts) {
    for (auto& draft : drafts) {
      VersionLayer layer;
      layer.property = property;
      layer.unconstrained = UnconstrainedBits(draft, nv);

      for (const auto& [var, c] : draft.entries) {
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
      for (const auto& [var, c] : draft.entries) {
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
            layer.regions[r].Set(var);
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
      layer.unconstrained = UnconstrainedBits(draft, nv);
      layer.nodes.emplace_back();  // корень
      for (const auto& [var, c] : draft.entries) {
        for (const DomainConstraint::Pattern& p : c->Patterns()) {
          uint32_t cur = 0;
          if (p.tld_wildcard) cur = layer.AddTldChild(cur);
          for (const std::string& label : p.suffix) {
            cur = layer.AddChild(cur, label);
          }
          auto& list =
              p.star ? layer.nodes[cur].star_vars : layer.nodes[cur].exact_vars;
          if (list.empty() || list.back() != var) list.push_back(var);
        }
      }
      domain_layers_.push_back(std::move(layer));
    }
  }

  for (auto& [property, drafts] : region_drafts) {
    for (auto& draft : drafts) {
      RegionLayer layer;
      layer.property = property;
      layer.unconstrained = UnconstrainedBits(draft, nv);
      layer.negated = DynamicBitset(nv);
      for (const auto& [var, c] : draft.entries) {
        if (!layer.tree) {
          layer.tree = c->Tree();
        } else if (layer.tree != c->Tree()) {
          throw std::invalid_argument(
              "region constraints for property '" + property +
              "' use different region trees; a single shared tree is required");
        }
        auto& lists = c->Negated() ? layer.not_in_lists : layer.in_lists;
        if (c->Negated()) layer.negated.Set(var);
        for (RegionTree::RegionId region : c->Regions()) {
          lists[region].push_back(var);
        }
      }
      region_layers_.push_back(std::move(layer));
    }
  }
}

std::vector<Assignment> IndexedMatcher::Match(const Request& request) const {
  std::vector<Assignment> result;
  if (experiments_.empty()) return result;
  const size_t nv = variant_exp_.size();

  DynamicBitset acc(nv);
  acc.SetAll();
  DynamicBitset scratch(nv);

  auto apply = [&](const auto& layers) {
    for (const auto& layer : layers) {
      layer.Filter(request, scratch);
      acc &= scratch;
      if (!acc.Any()) return false;
    }
    return true;
  };
  if (!apply(string_layers_) || !apply(bool_layers_) ||
      !apply(version_layers_) || !apply(domain_layers_) ||
      !apply(region_layers_)) {
    return result;
  }

  // Кеш хешей на запрос: разбиения с одинаковой тройкой (идентификатор,
  // алгоритм, соль) — например, эксперименты одного измерения — переиспользуют
  // вычисленный хеш.
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
        entry.hash = SaltedHash(*key.hasher, it->second, key.salt);
      }
    }
    return entry;
  };

  // Варианты одного эксперимента лежат подряд, биты обходятся по возрастанию,
  // поэтому для дедупликации достаточно помнить последний эксперимент.
  size_t last_exp = SIZE_MAX;
  acc.ForEachSet([&](size_t v) {
    size_t i = variant_exp_[v];
    if (i == last_exp) return;
    last_exp = i;

    const FlatExperiment& exp = experiments_[i];
    const ExperimentHashRefs& refs = hash_refs_[i];
    for (size_t s = 0; s < exp.slot_checks.size(); ++s) {
      const CacheEntry& entry = salted_hash(refs.slot_keys[s]);
      if (entry.state != CacheState::kReady ||
          !SlotAllowed(exp.slot_checks[s], entry.hash)) {
        return;
      }
    }
    const CacheEntry& own = salted_hash(refs.bucket_key);
    if (own.state != CacheState::kReady) return;
    auto group = GroupForBucket(exp, own.hash);
    if (!group) return;
    result.push_back(Assignment{&experiments_[i], *group,
                                MatchedSections(exp.groups[*group], request)});
  });
  return result;
}

}  // namespace expassign
