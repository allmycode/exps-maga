#include "expassign/config_loader.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include "expassign/constraints/bool_constraint.hpp"
#include "expassign/constraints/domain_constraint.hpp"
#include "expassign/constraints/region_constraint.hpp"
#include "expassign/constraints/string_constraint.hpp"
#include "expassign/constraints/version_constraint.hpp"
#include "json.hpp"

namespace expassign {
namespace {

using json::Value;

[[noreturn]] void Fail(const std::string& context, const std::string& what) {
  throw std::invalid_argument("experiment config: " + context + ": " + what);
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

std::vector<std::string> SplitCsv(std::string_view text) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t comma = text.find(',', pos);
    size_t end = comma == std::string_view::npos ? text.size() : comma;
    std::string_view piece = Trim(text.substr(pos, end - pos));
    if (!piece.empty()) out.emplace_back(piece);
    if (comma == std::string_view::npos) break;
    pos = comma + 1;
  }
  return out;
}

const Value& Require(const Value& obj, std::string_view key,
                     const std::string& context) {
  const Value* v = obj.Find(key);
  if (!v) Fail(context, "missing field '" + std::string(key) + "'");
  return *v;
}

std::string GetString(const Value& v, const std::string& context) {
  if (v.Is(Value::Type::kString)) return v.str;
  if (v.Is(Value::Type::kNumber)) {
    // testid и id встречаются и числами, и строками.
    double rounded = std::round(v.number);
    if (rounded == v.number) {
      return std::to_string(static_cast<long long>(rounded));
    }
  }
  Fail(context, "expected a string");
}

uint32_t GetUint(const Value& v, const std::string& context) {
  if (!v.Is(Value::Type::kNumber) || v.number < 0 ||
      std::round(v.number) != v.number || v.number > 4294967295.0) {
    Fail(context, "expected a non-negative integer");
  }
  return static_cast<uint32_t>(v.number);
}

// Соль "XXH3:abcd" -> {алгоритм "XXH3", соль "abcd"}; без ':' алгоритм "".
std::pair<std::string, std::string> ParseSalt(const std::string& salt) {
  size_t colon = salt.find(':');
  if (colon == std::string::npos) return {"", salt};
  return {salt.substr(0, colon), salt.substr(colon + 1)};
}

// Формат конфига: интервалы через ',', границы внутри интервала через ';',
// "+inf"/"-inf"/"inf"/пусто — бесконечность. Пример: "[2025.12.3;+inf)".
std::vector<VersionInterval> ParseConfigIntervals(std::string_view spec,
                                                  const std::string& context) {
  std::vector<VersionInterval> out;
  size_t pos = 0;
  while (pos <= spec.size()) {
    size_t comma = spec.find(',', pos);
    size_t end = comma == std::string_view::npos ? spec.size() : comma;
    std::string_view piece = Trim(spec.substr(pos, end - pos));
    pos = comma == std::string_view::npos ? spec.size() + 1 : comma + 1;
    if (piece.empty()) {
      if (comma == std::string_view::npos) break;
      Fail(context, "empty version interval in '" + std::string(spec) + "'");
    }
    if (piece.size() < 3 || (piece.front() != '[' && piece.front() != '(') ||
        (piece.back() != ']' && piece.back() != ')')) {
      Fail(context, "malformed version interval '" + std::string(piece) + "'");
    }
    VersionInterval iv;
    iv.lo_inclusive = piece.front() == '[';
    iv.hi_inclusive = piece.back() == ']';
    std::string_view inner = piece.substr(1, piece.size() - 2);
    size_t sep = inner.find(';');
    if (sep == std::string_view::npos ||
        inner.find(';', sep + 1) != std::string_view::npos) {
      Fail(context, "version interval must contain exactly one ';': '" +
                        std::string(piece) + "'");
    }
    auto parse_bound = [&](std::string_view text) -> std::optional<Version> {
      text = Trim(text);
      if (text.empty() || text == "inf" || text == "+inf" || text == "-inf") {
        return std::nullopt;
      }
      auto v = Version::Parse(text);
      if (!v) {
        Fail(context, "bad version bound '" + std::string(text) + "' in '" +
                          std::string(piece) + "'");
      }
      return v;
    };
    iv.lo = parse_bound(inner.substr(0, sep));
    iv.hi = parse_bound(inner.substr(sep + 1));
    out.push_back(std::move(iv));
  }
  if (out.empty()) Fail(context, "empty version interval spec");
  return out;
}

class Loader {
 public:
  explicit Loader(const ConfigSchema& schema) : schema_(schema) {}

  std::vector<FlatExperiment> Load(const Value& root) {
    if (!root.Is(Value::Type::kArray)) {
      Fail("root", "expected an array of experiment entries");
    }
    std::vector<FlatExperiment> out;
    for (size_t i = 0; i < root.array.size(); ++i) {
      out.push_back(LoadEntry(root.array[i], i));
    }
    ValidateSharedDimensions(out);
    return out;
  }

 private:
  PropertyType KeyType(const std::string& key) const {
    auto it = schema_.key_types.find(key);
    if (it != schema_.key_types.end()) return it->second;
    if (key.size() > 9 && key.ends_with("_versions")) {
      return PropertyType::kVersion;
    }
    return PropertyType::kString;
  }

  ConstraintPtr MakeConstraint(const std::string& key, const Value& value,
                               const std::string& context) const {
    std::string text = GetString(value, context + ", key '" + key + "'");
    const std::string ctx = context + ", key '" + key + "'";
    switch (KeyType(key)) {
      case PropertyType::kString: {
        auto values = SplitCsv(text);
        if (values.empty()) Fail(ctx, "empty value list");
        return std::make_shared<StringConstraint>(key, std::move(values));
      }
      case PropertyType::kBool: {
        std::string_view t = Trim(text);
        if (t != "true" && t != "false") {
          Fail(ctx, "expected 'true' or 'false', got '" + text + "'");
        }
        return std::make_shared<BoolConstraint>(key, t == "true");
      }
      case PropertyType::kVersion:
        return std::make_shared<VersionConstraint>(
            key, ParseConfigIntervals(text, ctx));
      case PropertyType::kDomain:
        return std::make_shared<DomainConstraint>(key, SplitCsv(text));
      case PropertyType::kRegion: {
        if (!schema_.region_tree) {
          Fail(ctx, "region constraint requires ConfigSchema::region_tree");
        }
        std::vector<RegionTree::RegionId> regions;
        for (const std::string& id_text : SplitCsv(text)) {
          try {
            size_t consumed = 0;
            unsigned long id = std::stoul(id_text, &consumed);
            if (consumed != id_text.size() || id > 4294967295UL) throw 0;
            regions.push_back(static_cast<RegionTree::RegionId>(id));
          } catch (...) {
            Fail(ctx, "bad region id '" + id_text + "'");
          }
        }
        if (regions.empty()) Fail(ctx, "empty region list");
        return std::make_shared<RegionConstraint>(key, schema_.region_tree,
                                                  std::move(regions));
      }
    }
    Fail(ctx, "unsupported constraint type");
  }

  // Блок restrictions (объект) -> AND-список ограничений.
  std::vector<ConstraintPtr> LoadBlock(const Value& block,
                                       const std::string& context) const {
    if (!block.Is(Value::Type::kObject)) {
      Fail(context, "restriction block must be an object");
    }
    std::vector<ConstraintPtr> out;
    for (const auto& [key, value] : block.object) {
      out.push_back(MakeConstraint(key, value, context));
    }
    return out;
  }

  // restrictions: массив блоков (OR) или один блок-объект.
  std::vector<std::vector<ConstraintPtr>> LoadRestrictions(
      const Value* restrictions, const std::string& context) const {
    std::vector<std::vector<ConstraintPtr>> out;
    if (!restrictions || restrictions->Is(Value::Type::kNull)) return out;
    if (restrictions->Is(Value::Type::kObject)) {
      auto block = LoadBlock(*restrictions, context);
      if (!block.empty()) out.push_back(std::move(block));
      return out;
    }
    if (!restrictions->Is(Value::Type::kArray)) {
      Fail(context, "restrictions must be an object or an array of objects");
    }
    for (const Value& block : restrictions->array) {
      out.push_back(LoadBlock(block, context));
    }
    // Пустой блок означает «без ограничений» и поглощает остальные варианты.
    for (const auto& block : out) {
      if (block.empty()) return {};
    }
    return out;
  }

  Section LoadSection(const Value& section, const std::string& context) const {
    if (!section.Is(Value::Type::kObject)) {
      Fail(context, "section must be an object");
    }
    Section out;
    out.restrictions = LoadRestrictions(section.Find("restrictions"), context);
    if (const Value* params = section.Find("params")) {
      out.params = GetString(*params, context + ", params");
    }
    return out;
  }

  FlatExperiment LoadEntry(const Value& entry, size_t index) const {
    const std::string context = "entry #" + std::to_string(index);
    if (!entry.Is(Value::Type::kObject)) Fail(context, "expected an object");

    FlatExperiment exp;
    exp.id = "entry-" + std::to_string(index);
    if (const Value* type = entry.Find("type")) {
      exp.type = GetString(*type, context + ", type");
    }
    exp.restrictions =
        LoadRestrictions(entry.Find("restrictions"), context + ", restrictions");

    const Value& places = Require(entry, "places", context);
    if (!places.Is(Value::Type::kArray) || places.array.empty()) {
      Fail(context, "places must be a non-empty array");
    }
    for (size_t p = 0; p < places.array.size(); ++p) {
      const std::string pctx = context + ", place #" + std::to_string(p);
      const Value& place = places.array[p];
      if (!place.Is(Value::Type::kObject)) Fail(pctx, "expected an object");

      auto [algo, salt] =
          ParseSalt(GetString(Require(place, "salt", pctx), pctx + ", salt"));
      std::string id_key = GetString(Require(place, "id", pctx), pctx + ", id");
      uint32_t size = GetUint(Require(place, "size", pctx), pctx + ", size");
      if (size == 0) Fail(pctx, "size must be positive");

      const bool last = p + 1 == places.array.size();
      const Value* slots = place.Find("slots");
      if (last) {
        if (slots) {
          Fail(pctx,
               "the last place defines the group split and must not have "
               "'slots'");
        }
        exp.id_key = std::move(id_key);
        exp.hash_algo = std::move(algo);
        exp.salt = std::move(salt);
        exp.total_buckets = size;
      } else {
        if (!slots) Fail(pctx, "non-last place must have 'slots'");
        SlotCheck check;
        check.id_key = std::move(id_key);
        check.hash_algo = std::move(algo);
        check.salt = std::move(salt);
        check.total_slots = size;
        for (const std::string& s :
             SplitCsv(GetString(*slots, pctx + ", slots"))) {
          try {
            size_t consumed = 0;
            unsigned long slot = std::stoul(s, &consumed);
            if (consumed != s.size()) throw 0;
            check.slots.push_back(static_cast<uint32_t>(slot));
          } catch (...) {
            Fail(pctx, "bad slot '" + s + "'");
          }
        }
        if (check.slots.empty()) Fail(pctx, "empty slots");
        std::sort(check.slots.begin(), check.slots.end());
        if (std::adjacent_find(check.slots.begin(), check.slots.end()) !=
            check.slots.end()) {
          Fail(pctx, "duplicate slots");
        }
        if (check.slots.back() >= check.total_slots) {
          Fail(pctx, "slot " + std::to_string(check.slots.back()) +
                         " >= size " + std::to_string(check.total_slots));
        }
        if (const Value* count = place.Find("slots_count")) {
          if (GetUint(*count, pctx + ", slots_count") != check.slots.size()) {
            Fail(pctx, "slots_count does not match the number of slots");
          }
        }
        exp.slot_checks.push_back(std::move(check));
      }
    }

    const Value& testids = Require(entry, "testids", context);
    if (!testids.Is(Value::Type::kArray) || testids.array.empty()) {
      Fail(context, "testids must be a non-empty array");
    }
    for (size_t t = 0; t < testids.array.size(); ++t) {
      const std::string tctx = context + ", testid #" + std::to_string(t);
      const Value& testid = testids.array[t];
      if (!testid.Is(Value::Type::kObject)) Fail(tctx, "expected an object");

      GroupRange group;
      group.name =
          GetString(Require(testid, "testid", tctx), tctx + ", testid");
      uint32_t slot = GetUint(Require(testid, "slot", tctx), tctx + ", slot");
      if (slot >= exp.total_buckets) {
        Fail(tctx, "slot " + std::to_string(slot) + " >= size of last place " +
                       std::to_string(exp.total_buckets));
      }
      group.begin = slot;
      group.end = slot + 1;
      if (const Value* sections = testid.Find("sections")) {
        if (!sections->Is(Value::Type::kArray)) {
          Fail(tctx, "sections must be an array");
        }
        for (size_t s = 0; s < sections->array.size(); ++s) {
          group.sections.push_back(LoadSection(
              sections->array[s], tctx + ", section #" + std::to_string(s)));
        }
      }
      exp.groups.push_back(std::move(group));
    }
    std::sort(exp.groups.begin(), exp.groups.end(),
              [](const GroupRange& a, const GroupRange& b) {
                return a.begin < b.begin;
              });
    for (size_t g = 1; g < exp.groups.size(); ++g) {
      if (exp.groups[g].begin < exp.groups[g - 1].end) {
        Fail(context, "testids occupy the same slot " +
                          std::to_string(exp.groups[g].begin));
      }
    }
    return exp;
  }

  // Места с одинаковой тройкой (id, алгоритм+соль, size) — общее измерение:
  // их слоты не должны пересекаться между записями.
  static void ValidateSharedDimensions(const std::vector<FlatExperiment>& exps) {
    std::unordered_map<std::string, std::unordered_set<uint32_t>> used;
    for (const FlatExperiment& exp : exps) {
      for (const SlotCheck& check : exp.slot_checks) {
        std::string key = check.id_key + '\x1f' + check.hash_algo + '\x1f' +
                          check.salt + '\x1f' + std::to_string(check.total_slots);
        auto& slots = used[key];
        for (uint32_t s : check.slots) {
          if (!slots.insert(s).second) {
            throw std::invalid_argument(
                "experiment config: slot " + std::to_string(s) +
                " of shared dimension (id " + check.id_key + ", salt '" +
                check.salt + "') is occupied by more than one experiment");
          }
        }
      }
    }
  }

  const ConfigSchema& schema_;
};

}  // namespace

ConfigSchema ConfigSchema::Default() {
  ConfigSchema schema;
  schema.key_types.emplace("regions", PropertyType::kRegion);
  return schema;
}

std::vector<FlatExperiment> LoadExperimentsJson(std::string_view json_text,
                                                const ConfigSchema& schema) {
  return Loader(schema).Load(json::Parse(json_text));
}

}  // namespace expassign
