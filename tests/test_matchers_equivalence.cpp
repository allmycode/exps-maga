// Рандомизированная проверка эквивалентности: наивный матчер (эталон,
// проверка ограничений «в лоб») против индексного. На случайных конфигурациях
// и случайных запросах результаты должны совпадать бит в бит.

#include <cstdio>
#include <random>

#include "expassign/expassign.hpp"
#include "minitest.hpp"

using namespace expassign;

namespace {

struct Generator {
  std::mt19937_64 rng;

  explicit Generator(uint64_t seed) : rng(seed) {
    // Случайное дерево из 30 регионов: регион 1 — корень, родитель региона
    // i — случайный регион с меньшим номером.
    tree = std::make_shared<RegionTree>();
    tree->AddRegion(1, 1);
    for (uint32_t r = 2; r <= 30; ++r) {
      tree->AddRegion(r, 1 + static_cast<uint32_t>(Uniform(r - 1)));
    }
    // Диапазоны IP на часть регионов; часть адресного пространства остаётся
    // непокрытой.
    for (int i = 0; i < 10; ++i) {
      std::string base = "10.0." + std::to_string(i) + ".";
      tree->AddIpRange(base + "0", base + "255",
                       1 + static_cast<uint32_t>(Uniform(30)));
    }
    tree->Build();
  }

  size_t Uniform(size_t n) { return rng() % n; }
  bool Chance(double p) {
    return std::uniform_real_distribution<double>(0, 1)(rng) < p;
  }

  template <typename T>
  const T& Pick(const std::vector<T>& v) {
    return v[Uniform(v.size())];
  }

  // Пулы имён и значений намеренно небольшие, чтобы ограничения и запросы
  // часто пересекались.
  std::vector<std::string> string_props{"service", "country", "platform"};
  std::vector<std::string> string_values{"web", "ios", "android",
                                         "tv",  "api", "bot"};
  std::vector<std::string> bool_props{"internal", "beta", "listed"};
  std::vector<std::string> version_props{"app_version", "os_version"};
  std::vector<std::string> domain_props{"host", "referer"};
  std::vector<std::string> slds{"example", "test", "mail", "shop"};
  std::vector<std::string> tlds{"ru", "com", "net"};
  std::vector<std::string> subs{"www", "m", "api", "static"};
  std::vector<std::string> id_keys{"uid", "device_id"};
  std::vector<std::string> region_props{"geo", "geo_billing"};
  std::shared_ptr<RegionTree> tree;

  Version RandomVersion() {
    size_t n = 1 + Uniform(4);
    std::vector<uint32_t> parts;
    for (size_t i = 0; i < n; ++i) {
      parts.push_back(static_cast<uint32_t>(Uniform(4)));
    }
    return Version(std::move(parts));
  }

  std::string RandomHost() {
    std::string host = Pick(slds) + "." + Pick(tlds);
    size_t extra = Uniform(3);
    for (size_t i = 0; i < extra; ++i) host = Pick(subs) + "." + host;
    if (Chance(0.05)) host = Pick(tlds);  // хост из одной метки
    if (Chance(0.05)) host += ".";        // FQDN
    return host;
  }

  std::string RandomDomainPattern() {
    std::string p = Pick(slds);
    if (Chance(0.3)) p = Pick(subs) + "." + p;
    p += Chance(0.5) ? ".{tld}" : "." + Pick(tlds);
    if (Chance(0.6)) p = "*." + p;
    return p;
  }

  ConstraintPtr RandomConstraint() {
    switch (Uniform(5)) {
      case 0: {
        std::vector<std::string> values;
        size_t n = 1 + Uniform(3);
        for (size_t i = 0; i < n; ++i) values.push_back(Pick(string_values));
        return std::make_shared<StringConstraint>(Pick(string_props),
                                                  std::move(values),
                                                  /*negated=*/Chance(0.5));
      }
      case 1:
        return std::make_shared<BoolConstraint>(Pick(bool_props), Chance(0.5));
      case 2: {
        std::vector<VersionInterval> intervals;
        size_t n = 1 + Uniform(2);
        for (size_t i = 0; i < n; ++i) {
          VersionInterval iv;
          if (Chance(0.85)) iv.lo = RandomVersion();
          if (Chance(0.85)) iv.hi = RandomVersion();
          if (iv.lo && iv.hi && *iv.hi < *iv.lo) std::swap(*iv.lo, *iv.hi);
          iv.lo_inclusive = Chance(0.5);
          iv.hi_inclusive = Chance(0.5);
          intervals.push_back(std::move(iv));
        }
        return std::make_shared<VersionConstraint>(Pick(version_props),
                                                   std::move(intervals));
      }
      case 3: {
        std::vector<std::string> patterns;
        size_t n = 1 + Uniform(2);
        for (size_t i = 0; i < n; ++i) patterns.push_back(RandomDomainPattern());
        return std::make_shared<DomainConstraint>(Pick(domain_props), patterns);
      }
      default: {
        std::vector<RegionTree::RegionId> regions;
        size_t n = 1 + Uniform(3);
        for (size_t i = 0; i < n; ++i) {
          // Иногда регион, неизвестный дереву (31..34).
          regions.push_back(1 + static_cast<uint32_t>(Uniform(34)));
        }
        return std::make_shared<RegionConstraint>(Pick(region_props), tree,
                                                  std::move(regions),
                                                  /*negated=*/Chance(0.5));
      }
    }
  }

  ExperimentConfig RandomExperiment(int index) {
    ExperimentConfig e;
    e.id = "exp-" + std::to_string(index);
    e.id_key = Pick(id_keys);
    e.salt = "salt-" + std::to_string(Uniform(5));
    e.total_buckets = 10 + static_cast<uint32_t>(Uniform(190));
    uint32_t left = e.total_buckets;
    size_t n_groups = 1 + Uniform(3);
    for (size_t g = 0; g < n_groups && left > 0; ++g) {
      uint32_t size = 1 + static_cast<uint32_t>(Uniform(left));
      e.groups.push_back({"g" + std::to_string(g), size});
      left -= size;
    }
    size_t n_constraints = Uniform(4);
    for (size_t i = 0; i < n_constraints; ++i) {
      e.constraints.push_back(RandomConstraint());
    }
    return e;
  }

  Config RandomConfig() {
    Config config;
    int exp_index = 0;
    size_t n_dims = 1 + Uniform(4);
    for (size_t d = 0; d < n_dims; ++d) {
      DimensionConfig dim;
      dim.id = "dim-" + std::to_string(d);
      dim.id_key = Pick(id_keys);
      dim.salt = "dim-salt-" + std::to_string(d);
      dim.total_slots = 1 + static_cast<uint32_t>(Uniform(16));
      size_t n_constraints = Uniform(3);
      for (size_t i = 0; i < n_constraints; ++i) {
        dim.constraints.push_back(RandomConstraint());
      }
      // Случайно раздаём непересекающиеся слоты экспериментам измерения.
      std::vector<uint32_t> slot_pool(dim.total_slots);
      for (uint32_t s = 0; s < dim.total_slots; ++s) slot_pool[s] = s;
      std::shuffle(slot_pool.begin(), slot_pool.end(), rng);
      size_t cursor = 0;
      size_t n_exps = Uniform(20);
      for (size_t i = 0; i < n_exps && cursor < slot_pool.size(); ++i) {
        ExperimentConfig e = RandomExperiment(exp_index++);
        size_t take = 1 + Uniform(3);
        for (size_t s = 0; s < take && cursor < slot_pool.size(); ++s) {
          e.slots.push_back(slot_pool[cursor++]);
        }
        dim.experiments.push_back(std::move(e));
      }
      config.dimensions.push_back(std::move(dim));
    }
    size_t n_free = Uniform(10);
    for (size_t i = 0; i < n_free; ++i) {
      config.experiments.push_back(RandomExperiment(exp_index++));
    }
    return config;
  }

  Request RandomRequest() {
    Request req;
    for (const auto& p : string_props) {
      if (Chance(0.75)) req.strings[p] = Pick(string_values);
    }
    for (const auto& p : bool_props) {
      if (Chance(0.75)) req.bools[p] = Chance(0.5);
    }
    for (const auto& p : version_props) {
      if (Chance(0.75)) req.versions[p] = RandomVersion();
    }
    for (const auto& p : domain_props) {
      if (Chance(0.75)) req.domains[p] = RandomHost();
    }
    for (const auto& p : region_props) {
      // Иногда явный id (в т.ч. неизвестный дереву), иногда только IP.
      if (Chance(0.5)) req.regions[p] = 1 + static_cast<uint32_t>(Uniform(34));
    }
    if (Chance(0.6)) {
      if (Chance(0.1)) {
        req.ip = "not-an-ip";
      } else {
        // Иногда адрес вне всех диапазонов (10.0.10.x - 10.0.12.x).
        req.ip = "10.0." + std::to_string(Uniform(13)) + "." +
                 std::to_string(Uniform(256));
      }
    }
    for (const auto& k : id_keys) {
      if (Chance(0.9)) req.ids[k] = "user-" + std::to_string(Uniform(10000));
    }
    return req;
  }
};

bool SameAssignments(const std::vector<Assignment>& a,
                     const std::vector<Assignment>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].ExperimentId() != b[i].ExperimentId() ||
        a[i].group_index != b[i].group_index) {
      return false;
    }
  }
  return true;
}

}  // namespace

TEST(randomized_equivalence) {
  auto hasher = std::make_shared<XXHash64Hasher>();
  size_t total_assignments = 0;
  size_t nonempty_requests = 0;

  for (uint64_t seed = 1; seed <= 25; ++seed) {
    Generator gen(seed);
    Config config = gen.RandomConfig();
    auto flat = Flatten(config);
    NaiveMatcher naive(flat, hasher);
    IndexedMatcher indexed(flat, hasher);

    for (int q = 0; q < 800; ++q) {
      Request req = gen.RandomRequest();
      auto a = naive.Match(req);
      auto b = indexed.Match(req);
      if (!SameAssignments(a, b)) {
        std::printf("  mismatch: seed=%llu query=%d naive=%zu indexed=%zu\n",
                    static_cast<unsigned long long>(seed), q, a.size(),
                    b.size());
        CHECK(false);
        return;
      }
      total_assignments += a.size();
      if (!a.empty()) ++nonempty_requests;
    }
  }
  // Убеждаемся, что тест реально что-то назначал, а не сравнивал пустоту.
  std::printf("  assignments=%zu nonempty_requests=%zu\n", total_assignments,
              nonempty_requests);
  CHECK(total_assignments > 1000);
  CHECK(nonempty_requests > 500);
}

TEST(empty_matcher) {
  auto hasher = std::make_shared<XXHash64Hasher>();
  NaiveMatcher naive({}, hasher);
  IndexedMatcher indexed({}, hasher);
  Request req;
  req.ids["uid"] = "u";
  CHECK(naive.Match(req).empty());
  CHECK(indexed.Match(req).empty());
}

TEST_MAIN()
