// Тесты загрузчика JSON-конфига экспериментов: структура, валидация и
// сквозной матчинг с эквивалентностью наивного и индексного матчеров.

#include <cstdio>
#include <random>
#include <stdexcept>

#include "expassign/expassign.hpp"
#include "minitest.hpp"

using namespace expassign;

namespace {

// Конфиг в формате продакшена (см. постановку задачи).
const char* kConfig = R"json([
    {
        "restrictions": [
            {
                "services": "serv1, serv1-mobile"
            }
        ],
        "places": [
            {
                "salt": "XXH3:12cb387f84d6ae6b320515355a742aa9",
                "id": 64,
                "size": 100,
                "slots": "0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14",
                "slots_count": 15,
                "percent": 15.0
            },
            {
                "salt": "XXH3:3242530d9aabc7a4838858a1344042b9",
                "id": 64,
                "size": 3
            }
        ],
        "testids": [
            {
                "slot": 0,
                "testid": "1467025",
                "percent": 5.0,
                "sections": [
                    {
                        "params": "some data"
                    }
                ]
            },
            {
                "slot": 1,
                "testid": "1467026",
                "percent": 5.0,
                "sections": [
                    {
                        "params": "some data2"
                    }
                ]
            },
            {
                "slot": 2,
                "testid": "1467027",
                "percent": 5.0,
                "sections": [
                    {
                        "params": "some data3"
                    }
                ]
            }
        ],
        "type": "ABT"
    },
    {
        "restrictions": [
            {
                "services": "serv1, serv2, serv3"
            }
        ],
        "places": [
            {
                "salt": "XXH3:12cb387f84d6ae6b320515355a742aa9",
                "id": 64,
                "size": 100,
                "slots": "15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38",
                "slots_count": 24,
                "percent": 24.0
            },
            {
                "salt": "XXH3:e2b100627067fa3138bfadf23d3d3af3",
                "id": 64,
                "size": 3
            }
        ],
        "testids": [
            {
                "slot": 0,
                "testid": "1467091",
                "percent": 8.0,
                "sections": [
                    {
                        "params": "exp flags"
                    }
                ]
            },
            {
                "slot": 1,
                "testid": "1467092",
                "percent": 8.0,
                "sections": [
                    {
                        "params": "exp flags2"
                    }
                ]
            },
            {
                "slot": 2,
                "testid": "1467093",
                "percent": 8.0,
                "sections": [
                    {
                        "params": "exp flags3"
                    }
                ]
            }
        ],
        "type": "ABT"
    },
    {
        "restrictions": [
            {
                "services": "some_service, other_service, books, web",
                "regions": "115",
                "browsers": "Opera",
                "networks": "external",
                "browser__build_types": "beta",
                "browser__app_versions": "[2025.12.3;+inf)",
                "browser__platforms": "ios",
                "browser__device_types": "phone, tablet",
                "browser__geo_countries": "RU"
            }
        ],
        "places": [
            {
                "salt": "4f3e52118365919363752309e3b86333",
                "id": 64,
                "size": 100,
                "slots": "0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99",
                "slots_count": 100,
                "percent": 100.0
            },
            {
                "salt": "1666a3cea2ac49bec2b1bce58bb3d913",
                "id": 64,
                "size": 2
            }
        ],
        "testids": [
            {
                "slot": 0,
                "testid": "1459089",
                "percent": 50.0,
                "sections": [
                    {
                        "restrictions": {
                            "services": "some_service, other_service"
                        },
                        "params": "exp params"
                    }
                ]
            },
            {
                "slot": 1,
                "testid": "1459090",
                "percent": 50.0,
                "sections": [
                    {
                        "restrictions": {
                            "services": "some_service, other_service"
                        },
                        "params": "exp params2"
                    }
                ]
            }
        ],
        "type": "ABT"
    }
])json";

ConfigSchema MakeSchema() {
  auto tree = std::make_shared<RegionTree>();
  tree->AddRegion(10000, 10000);  // мир
  tree->AddRegion(225, 10000);    // страна
  tree->AddRegion(115, 225);      // регион из конфига
  tree->AddRegion(213, 115);      // город внутри 115
  tree->AddRegion(84, 10000);     // другой регион
  tree->AddIpRange("10.5.0.0", "10.5.0.255", 213);
  tree->Build();

  ConfigSchema schema = ConfigSchema::Default();
  schema.region_tree = tree;
  return schema;
}

// Запрос, полностью удовлетворяющий ограничениям третьей записи конфига.
Request FullThirdEntryRequest(const std::string& user) {
  Request req;
  req.strings["services"] = "some_service";
  req.strings["browsers"] = "Opera";
  req.strings["networks"] = "external";
  req.strings["browser__build_types"] = "beta";
  req.strings["browser__platforms"] = "ios";
  req.strings["browser__device_types"] = "phone";
  req.strings["browser__geo_countries"] = "RU";
  req.versions["browser__app_versions"] = *Version::Parse("2025.12.3");
  req.regions["regions"] = 213;  // город внутри региона 115
  req.ids["64"] = user;
  return req;
}

}  // namespace

TEST(config_structure) {
  auto flat = LoadExperimentsJson(kConfig, MakeSchema());
  CHECK(flat.size() == 3);

  const FlatExperiment& e0 = flat[0];
  CHECK(e0.type == "ABT");
  CHECK(e0.restrictions.size() == 1 && e0.restrictions[0].size() == 1);
  CHECK(e0.restrictions[0][0]->Type() == PropertyType::kString);
  CHECK(e0.restrictions[0][0]->Property() == "services");
  CHECK(e0.slot_checks.size() == 1);
  CHECK(e0.slot_checks[0].hash_algo == "XXH3");
  CHECK(e0.slot_checks[0].salt == "12cb387f84d6ae6b320515355a742aa9");
  CHECK(e0.slot_checks[0].id_key == "64");
  CHECK(e0.slot_checks[0].total_slots == 100);
  CHECK(e0.slot_checks[0].slots.size() == 15);
  CHECK(e0.hash_algo == "XXH3");
  CHECK(e0.salt == "3242530d9aabc7a4838858a1344042b9");
  CHECK(e0.total_buckets == 3);
  CHECK(e0.groups.size() == 3);
  CHECK(e0.groups[0].name == "1467025" && e0.groups[0].begin == 0 &&
        e0.groups[0].end == 1);
  CHECK(e0.groups[2].name == "1467027" && e0.groups[2].begin == 2);
  CHECK(e0.groups[0].sections.size() == 1);
  CHECK(e0.groups[0].sections[0].params == "some data");
  CHECK(e0.groups[0].sections[0].restrictions.empty());

  // Записи 0 и 1 разделяют одно измерение (одинаковая соль первого места).
  const FlatExperiment& e1 = flat[1];
  CHECK(e1.slot_checks[0].salt == e0.slot_checks[0].salt);
  CHECK(e1.slot_checks[0].slots.front() == 15);
  CHECK(e1.slot_checks[0].slots.back() == 38);

  const FlatExperiment& e2 = flat[2];
  CHECK(e2.hash_algo.empty());  // соль без префикса — алгоритм по умолчанию
  CHECK(e2.salt == "1666a3cea2ac49bec2b1bce58bb3d913");
  CHECK(e2.restrictions.size() == 1 && e2.restrictions[0].size() == 9);
  bool has_version = false;
  bool has_region = false;
  for (const ConstraintPtr& c : e2.restrictions[0]) {
    if (c->Type() == PropertyType::kVersion) {
      has_version = true;
      const auto* vc = static_cast<const VersionConstraint*>(c.get());
      CHECK(vc->Property() == "browser__app_versions");
      CHECK(vc->Intervals().size() == 1);
      CHECK(vc->Intervals()[0].lo &&
            vc->Intervals()[0].lo->ToString() == "2025.12.3");
      CHECK(vc->Intervals()[0].lo_inclusive);
      CHECK(!vc->Intervals()[0].hi);
    }
    if (c->Type() == PropertyType::kRegion) {
      has_region = true;
      const auto* rc = static_cast<const RegionConstraint*>(c.get());
      CHECK(rc->Regions().contains(115));
    }
  }
  CHECK(has_version && has_region);
  CHECK(e2.groups.size() == 2);
  CHECK(e2.groups[0].sections.size() == 1);
  CHECK(e2.groups[0].sections[0].restrictions.size() == 1);
}

TEST(config_validation_errors) {
  ConfigSchema schema = MakeSchema();

  // Пересечение слотов общего измерения между записями.
  const char* overlapping = R"json([
    {"places": [{"salt": "S:a", "id": 1, "size": 10, "slots": "0, 1"},
                {"salt": "S:b", "id": 1, "size": 2}],
     "testids": [{"slot": 0, "testid": "1"}]},
    {"places": [{"salt": "S:a", "id": 1, "size": 10, "slots": "1, 2"},
                {"salt": "S:c", "id": 1, "size": 2}],
     "testids": [{"slot": 0, "testid": "2"}]}
  ])json";
  CHECK_THROWS(LoadExperimentsJson(overlapping, schema));

  // У последнего места не должно быть slots.
  const char* last_with_slots = R"json([
    {"places": [{"salt": "a", "id": 1, "size": 10, "slots": "0"}],
     "testids": [{"slot": 0, "testid": "1"}]}
  ])json";
  CHECK_THROWS(LoadExperimentsJson(last_with_slots, schema));

  // slots_count не совпадает с числом слотов.
  const char* bad_count = R"json([
    {"places": [{"salt": "a", "id": 1, "size": 10, "slots": "0, 1", "slots_count": 3},
                {"salt": "b", "id": 1, "size": 2}],
     "testids": [{"slot": 0, "testid": "1"}]}
  ])json";
  CHECK_THROWS(LoadExperimentsJson(bad_count, schema));

  // Слот testid за пределами последнего места.
  const char* bad_testid_slot = R"json([
    {"places": [{"salt": "b", "id": 1, "size": 2}],
     "testids": [{"slot": 2, "testid": "1"}]}
  ])json";
  CHECK_THROWS(LoadExperimentsJson(bad_testid_slot, schema));

  // Два testid на одном слоте.
  const char* dup_testid_slot = R"json([
    {"places": [{"salt": "b", "id": 1, "size": 2}],
     "testids": [{"slot": 0, "testid": "1"}, {"slot": 0, "testid": "2"}]}
  ])json";
  CHECK_THROWS(LoadExperimentsJson(dup_testid_slot, schema));

  // Некорректная граница версии.
  const char* bad_version = R"json([
    {"restrictions": [{"browser__app_versions": "[abc;+inf)"}],
     "places": [{"salt": "b", "id": 1, "size": 2}],
     "testids": [{"slot": 0, "testid": "1"}]}
  ])json";
  CHECK_THROWS(LoadExperimentsJson(bad_version, schema));

  // Региональный ключ без дерева в схеме.
  const char* region_no_tree = R"json([
    {"restrictions": [{"regions": "115"}],
     "places": [{"salt": "b", "id": 1, "size": 2}],
     "testids": [{"slot": 0, "testid": "1"}]}
  ])json";
  CHECK_THROWS(LoadExperimentsJson(region_no_tree, ConfigSchema::Default()));

  // Битый JSON.
  CHECK_THROWS(LoadExperimentsJson("[{]", schema));
}

TEST(config_matching_end_to_end) {
  auto flat = LoadExperimentsJson(kConfig, MakeSchema());
  auto hashers = HasherRegistry::CreateDefault();
  NaiveMatcher naive(flat, hashers);
  IndexedMatcher indexed(flat, hashers);

  // Запрос, полностью удовлетворяющий третьей записи: первое место занимает
  // все 100 слотов, оба бакета второго заняты testid'ами, поэтому назначение
  // обязано быть ровно одно, а секция подходит (some_service).
  size_t hits_89 = 0;
  size_t hits_90 = 0;
  for (int u = 0; u < 300; ++u) {
    Request req = FullThirdEntryRequest("user-" + std::to_string(u));
    auto a = naive.Match(req);
    auto b = indexed.Match(req);
    CHECK(a.size() == 1 && b.size() == 1);
    if (a.empty() || b.empty()) return;
    CHECK(a[0].GroupName() == b[0].GroupName());
    CHECK(a[0].sections == (std::vector<uint32_t>{0}));
    CHECK(a[0].Group().sections[0].params.rfind("exp params", 0) == 0);
    if (a[0].GroupName() == "1459089") ++hits_89;
    if (a[0].GroupName() == "1459090") ++hits_90;
  }
  CHECK(hits_89 + hits_90 == 300);
  CHECK(hits_89 > 100 && hits_90 > 100);  // ~50/50

  // Сервис web проходит основные ограничения, но не ограничение секции.
  Request web = FullThirdEntryRequest("user-1");
  web.strings["services"] = "web";
  auto a = naive.Match(web);
  CHECK(a.size() == 1 && a[0].sections.empty());
  auto b = indexed.Match(web);
  CHECK(b.size() == 1 && b[0].sections.empty());

  // Версия ниже границы — третья запись не подходит.
  Request old_version = FullThirdEntryRequest("user-1");
  old_version.versions["browser__app_versions"] = *Version::Parse("2025.12.2");
  CHECK(naive.Match(old_version).empty());
  CHECK(indexed.Match(old_version).empty());

  // Регион вне 115 — не подходит.
  Request wrong_region = FullThirdEntryRequest("user-1");
  wrong_region.regions["regions"] = 84;
  CHECK(naive.Match(wrong_region).empty());
  CHECK(indexed.Match(wrong_region).empty());

  // Регион по IP (10.5.0.x -> город 213 внутри 115).
  Request by_ip = FullThirdEntryRequest("user-1");
  by_ip.regions.clear();
  by_ip.ip = "10.5.0.77";
  CHECK(naive.Match(by_ip).size() == 1);
  CHECK(indexed.Match(by_ip).size() == 1);

  // Эквивалентность на случайных запросах по всему конфигу.
  std::mt19937_64 rng(42);
  std::vector<std::string> services{"serv1",        "serv2", "serv3",
                                    "serv1-mobile", "web",   "some_service",
                                    "books",        "unknown"};
  size_t assignments = 0;
  for (int q = 0; q < 2000; ++q) {
    Request req;
    req.strings["services"] = services[rng() % services.size()];
    if (rng() % 2) req.strings["browsers"] = "Opera";
    if (rng() % 2) {
      req.strings["networks"] = "external";
      req.strings["browser__build_types"] = "beta";
      req.strings["browser__platforms"] = "ios";
      req.strings["browser__device_types"] = "tablet";
      req.strings["browser__geo_countries"] = "RU";
    }
    if (rng() % 2) {
      req.versions["browser__app_versions"] =
          *Version::Parse(rng() % 2 ? "2025.12.3" : "2024.1");
    }
    switch (rng() % 3) {
      case 0: req.regions["regions"] = rng() % 2 ? 213u : 84u; break;
      case 1: req.ip = "10.5.0." + std::to_string(rng() % 256); break;
      default: break;
    }
    if (rng() % 10 != 0) {
      req.ids["64"] = "user-" + std::to_string(rng() % 100000);
    }
    auto na = naive.Match(req);
    auto ia = indexed.Match(req);
    CHECK(na.size() == ia.size());
    for (size_t i = 0; i < std::min(na.size(), ia.size()); ++i) {
      CHECK(na[i].ExperimentId() == ia[i].ExperimentId());
      CHECK(na[i].group_index == ia[i].group_index);
      CHECK(na[i].sections == ia[i].sections);
    }
    assignments += na.size();
  }
  std::printf("  config equivalence assignments=%zu\n", assignments);
  CHECK(assignments > 200);
}

TEST_MAIN()
