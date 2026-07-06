// Бенчмарк: сколько времени занимает назначение экспериментов на запрос при
// десятках тысяч экспериментов. Сравнивает наивный и индексный матчеры.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

#include "expassign/expassign.hpp"

using namespace expassign;
using Clock = std::chrono::steady_clock;

namespace {

struct Generator {
  std::mt19937_64 rng{20260706};

  Generator() {
    // Дерево из 500 регионов + 200 диапазонов IP.
    tree = std::make_shared<RegionTree>();
    tree->AddRegion(1, 1);
    for (uint32_t r = 2; r <= 500; ++r) {
      tree->AddRegion(r, 1 + static_cast<uint32_t>(Uniform(r - 1)));
    }
    for (int i = 0; i < 200; ++i) {
      std::string base =
          "10." + std::to_string(i / 250) + "." + std::to_string(i % 250) + ".";
      tree->AddIpRange(base + "0", base + "255",
                       1 + static_cast<uint32_t>(Uniform(500)));
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

  std::vector<std::string> string_props{"service", "country",  "platform",
                                        "locale",  "ab_bucket", "channel"};
  std::vector<std::string> string_values = [] {
    std::vector<std::string> v;
    for (int i = 0; i < 40; ++i) v.push_back("value-" + std::to_string(i));
    return v;
  }();
  std::vector<std::string> bool_props{"internal", "beta", "listed", "vip"};
  std::vector<std::string> version_props{"app_version", "os_version"};
  std::vector<std::string> domain_props{"host"};
  std::vector<std::string> slds{"example", "test", "mail", "shop", "news"};
  std::vector<std::string> tlds{"ru", "com", "net", "org"};
  std::vector<std::string> subs{"www", "m", "api", "static", "cdn"};
  std::vector<std::string> region_props{"geo"};
  std::shared_ptr<RegionTree> tree;

  Version RandomVersion() {
    std::vector<uint32_t> parts;
    size_t n = 2 + Uniform(3);
    for (size_t i = 0; i < n; ++i) {
      parts.push_back(static_cast<uint32_t>(Uniform(20)));
    }
    return Version(std::move(parts));
  }

  ConstraintPtr RandomConstraint() {
    switch (Uniform(5)) {
      case 0: {
        std::vector<std::string> values;
        size_t n = 1 + Uniform(5);
        for (size_t i = 0; i < n; ++i) values.push_back(Pick(string_values));
        return std::make_shared<StringConstraint>(Pick(string_props),
                                                  std::move(values),
                                                  Chance(0.3));
      }
      case 1:
        return std::make_shared<BoolConstraint>(Pick(bool_props), Chance(0.5));
      case 2: {
        VersionInterval iv;
        iv.lo = RandomVersion();
        iv.hi = RandomVersion();
        if (*iv.hi < *iv.lo) std::swap(*iv.lo, *iv.hi);
        return std::make_shared<VersionConstraint>(
            Pick(version_props), std::vector<VersionInterval>{iv});
      }
      case 3: {
        std::vector<std::string> patterns;
        size_t n = 1 + Uniform(2);
        for (size_t i = 0; i < n; ++i) {
          std::string p = Pick(slds);
          if (Chance(0.3)) p = Pick(subs) + "." + p;
          p += Chance(0.5) ? ".{tld}" : "." + Pick(tlds);
          if (Chance(0.7)) p = "*." + p;
          patterns.push_back(std::move(p));
        }
        return std::make_shared<DomainConstraint>(Pick(domain_props), patterns);
      }
      default: {
        std::vector<RegionTree::RegionId> regions;
        size_t n = 1 + Uniform(3);
        for (size_t i = 0; i < n; ++i) {
          regions.push_back(1 + static_cast<uint32_t>(Uniform(500)));
        }
        return std::make_shared<RegionConstraint>(Pick(region_props), tree,
                                                  std::move(regions),
                                                  Chance(0.3));
      }
    }
  }

  Config BuildConfig(size_t n_dimensions, size_t exps_per_dim) {
    Config config;
    int exp_index = 0;
    for (size_t d = 0; d < n_dimensions; ++d) {
      DimensionConfig dim;
      dim.id = "dim-" + std::to_string(d);
      dim.id_key = "uid";
      dim.salt = "dim-salt-" + std::to_string(d);
      dim.total_slots = static_cast<uint32_t>(exps_per_dim * 2);
      size_t nc = Uniform(2);
      for (size_t i = 0; i < nc; ++i) dim.constraints.push_back(RandomConstraint());

      std::vector<uint32_t> slot_pool(dim.total_slots);
      for (uint32_t s = 0; s < dim.total_slots; ++s) slot_pool[s] = s;
      std::shuffle(slot_pool.begin(), slot_pool.end(), rng);
      size_t cursor = 0;
      for (size_t e = 0; e < exps_per_dim; ++e) {
        ExperimentConfig exp;
        exp.id = "exp-" + std::to_string(exp_index++);
        exp.id_key = "uid";
        exp.salt = "salt-" + std::to_string(exp_index);
        exp.total_buckets = 100;
        exp.groups = {{"control", 50}, {"treatment", 50}};
        exp.slots = {slot_pool[cursor++], slot_pool[cursor++]};
        size_t n_constraints = 2 + Uniform(3);
        for (size_t i = 0; i < n_constraints; ++i) {
          exp.constraints.push_back(RandomConstraint());
        }
        dim.experiments.push_back(std::move(exp));
      }
      config.dimensions.push_back(std::move(dim));
    }
    return config;
  }

  Request RandomRequest() {
    Request req;
    for (const auto& p : string_props) req.strings[p] = Pick(string_values);
    for (const auto& p : bool_props) req.bools[p] = Chance(0.5);
    for (const auto& p : version_props) req.versions[p] = RandomVersion();
    std::string host = Pick(slds) + "." + Pick(tlds);
    size_t extra = Uniform(3);
    for (size_t i = 0; i < extra; ++i) host = Pick(subs) + "." + host;
    req.domains["host"] = host;
    if (Chance(0.5)) {
      req.regions["geo"] = 1 + static_cast<uint32_t>(Uniform(500));
    } else {
      req.ip = "10.0." + std::to_string(Uniform(250)) + "." +
               std::to_string(Uniform(256));
    }
    req.ids["uid"] = "user-" + std::to_string(Uniform(1000000));
    return req;
  }
};

template <typename Matcher>
void RunBench(const char* name, const Matcher& matcher,
              const std::vector<Request>& requests) {
  size_t total_assignments = 0;
  std::vector<double> micros;
  micros.reserve(requests.size());
  for (const Request& req : requests) {
    auto start = Clock::now();
    auto assignments = matcher.Match(req);
    auto stop = Clock::now();
    total_assignments += assignments.size();
    micros.push_back(
        std::chrono::duration<double, std::micro>(stop - start).count());
  }
  std::sort(micros.begin(), micros.end());
  double sum = 0;
  for (double m : micros) sum += m;
  auto pct = [&](double p) {
    return micros[std::min(micros.size() - 1,
                           static_cast<size_t>(p * micros.size()))];
  };
  std::printf(
      "%-8s avg %8.1f us | p50 %8.1f us | p99 %8.1f us | max %8.1f us | "
      "assignments %zu\n",
      name, sum / micros.size(), pct(0.5), pct(0.99), micros.back(),
      total_assignments);
}

}  // namespace

int main() {
  Generator gen;
  auto hashers = HasherRegistry::CreateDefault();

  for (auto [dims, per_dim] : {std::pair<size_t, size_t>{10, 100},
                               {25, 400},
                               {50, 400},
                               {50, 600}}) {
    Config config = gen.BuildConfig(dims, per_dim);
    auto flat = Flatten(config);
    std::printf("--- %zu experiments (%zu dimensions x %zu) ---\n",
                flat.size(), dims, per_dim);

    auto build_start = Clock::now();
    IndexedMatcher indexed(flat, hashers);
    auto build_stop = Clock::now();
    std::printf("index build: %.1f ms\n",
                std::chrono::duration<double, std::milli>(build_stop -
                                                          build_start)
                    .count());
    NaiveMatcher naive(flat, hashers);

    std::vector<Request> requests;
    for (int i = 0; i < 2000; ++i) requests.push_back(gen.RandomRequest());
    // Прогрев кешей.
    for (int i = 0; i < 100; ++i) indexed.Match(requests[i]);

    RunBench("indexed", indexed, requests);
    RunBench("naive", naive, requests);
  }
  return 0;
}
