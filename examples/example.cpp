// Пример использования библиотеки: конфигурация с измерением и свободным
// экспериментом, назначение экспериментов на запрос.

#include <cstdio>

#include "expassign/expassign.hpp"

using namespace expassign;

int main() {
  Config config;

  // Дерево регионов: 1 (мир) <- 2 (Европа) <- 3 (Россия) <- 4 (Москва);
  // 5 (США) <- 1. Плюс диапазон IP, попадающий в Москву.
  auto geo = std::make_shared<RegionTree>();
  geo->AddRegion(1, 1);
  geo->AddRegion(2, 1);
  geo->AddRegion(3, 2);
  geo->AddRegion(4, 3);
  geo->AddRegion(5, 1);
  geo->AddIpRange("192.168.1.0", "192.168.1.255", 4);
  geo->Build();

  // Измерение: эксперименты над UI, взаимоисключающие между собой.
  DimensionConfig ui;
  ui.id = "ui-dimension";
  ui.id_key = "uid";
  ui.salt = "ui-2026";
  ui.total_slots = 100;
  ui.constraints = {
      std::make_shared<StringConstraint>(
          "service", std::vector<std::string>{"web", "mobile-web"}),
      std::make_shared<BoolConstraint>("internal", false),
  };

  ExperimentConfig header;
  header.id = "new-header";
  header.id_key = "uid";
  header.salt = "hdr-1";
  header.total_buckets = 100;
  header.groups = {{"control", 50}, {"treatment", 50}};
  for (uint32_t s = 0; s < 30; ++s) header.slots.push_back(s);  // 30% слотов
  header.constraints = {
      VersionConstraint::Parse("app_version", "[2.0,3.0); [3.1.5,]"),
      std::make_shared<DomainConstraint>(
          "host", std::vector<std::string>{"*.example.{tld}"}),
  };
  ui.experiments.push_back(header);

  ExperimentConfig footer;
  footer.id = "compact-footer";
  footer.id_key = "uid";
  footer.salt = "ftr-1";
  footer.total_buckets = 100;
  footer.groups = {{"control", 30}, {"treatment", 30}};  // 40 бакетов — holdout
  for (uint32_t s = 30; s < 60; ++s) footer.slots.push_back(s);
  ui.experiments.push_back(footer);

  config.dimensions.push_back(ui);

  // Свободный эксперимент вне измерений.
  ExperimentConfig ranking;
  ranking.id = "ranking-v2";
  ranking.id_key = "device_id";
  ranking.salt = "rank";
  ranking.total_buckets = 1000;
  ranking.groups = {{"on", 100}};  // 10% устройств
  ranking.constraints = {
      std::make_shared<StringConstraint>(
          "country", std::vector<std::string>{"kp"}, /*negated=*/true),
      // Только Европа; регион запроса определится по IP.
      std::make_shared<RegionConstraint>(
          "geo", geo, std::vector<RegionTree::RegionId>{2}),
  };
  config.experiments.push_back(ranking);

  auto flat = Flatten(config);
  IndexedMatcher matcher(flat, HasherRegistry::CreateDefault());

  Request req;
  req.strings["service"] = "web";
  req.strings["country"] = "de";
  req.bools["internal"] = false;
  req.versions["app_version"] = *Version::Parse("2.7.1.9000");
  req.domains["host"] = "www.example.com";
  req.ip = "192.168.1.10";  // Москва -> внутри Европы
  req.ids["uid"] = "user-39";
  req.ids["device_id"] = "device-39";

  for (const Assignment& a : matcher.Match(req)) {
    std::printf("experiment=%s group=%s\n", a.ExperimentId().c_str(),
                a.GroupName().c_str());
  }
  return 0;
}
