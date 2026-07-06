// Юнит-тесты базовых типов и классов ограничений.

#include <stdexcept>

#include "expassign/expassign.hpp"
#include "minitest.hpp"

using namespace expassign;

TEST(version_parse) {
  auto v = Version::Parse("1.2.3.4");
  CHECK(v.has_value());
  CHECK(v->Parts() == (std::vector<uint32_t>{1, 2, 3, 4}));
  CHECK(v->ToString() == "1.2.3.4");

  CHECK(Version::Parse("7").has_value());
  CHECK(Version::Parse("0.0.0").has_value());
  CHECK(!Version::Parse("").has_value());
  CHECK(!Version::Parse("1..2").has_value());
  CHECK(!Version::Parse("1.2.").has_value());
  CHECK(!Version::Parse(".1").has_value());
  CHECK(!Version::Parse("1.a").has_value());
  CHECK(!Version::Parse("1.-2").has_value());
  CHECK(!Version::Parse("99999999999").has_value());  // > uint32
}

TEST(version_compare) {
  auto v = [](const char* s) { return *Version::Parse(s); };
  CHECK(v("1.2") == v("1.2.0"));
  CHECK(v("1.2") < v("1.2.1"));
  CHECK(v("1.10") > v("1.9"));
  CHECK(v("2") > v("1.999.999"));
  CHECK(v("1.2.3.4") == v("1.2.3.4"));
  CHECK(v("0") == v("0.0.0.0"));
}

TEST(string_constraint) {
  Request req;
  req.strings["service"] = "web";

  StringConstraint in("service", {"web", "ios"});
  CHECK(in.Matches(req));
  StringConstraint not_in("service", {"web", "ios"}, /*negated=*/true);
  CHECK(!not_in.Matches(req));

  req.strings["service"] = "tv";
  CHECK(!in.Matches(req));
  CHECK(not_in.Matches(req));

  // Отсутствующее свойство — ограничение не выполнено в обоих режимах.
  Request empty;
  CHECK(!in.Matches(empty));
  CHECK(!not_in.Matches(empty));
}

TEST(bool_constraint) {
  Request req;
  req.bools["internal"] = true;
  CHECK(BoolConstraint("internal", true).Matches(req));
  CHECK(!BoolConstraint("internal", false).Matches(req));
  CHECK(!BoolConstraint("beta", true).Matches(Request{}));
}

TEST(version_intervals_parse) {
  auto ivs = VersionConstraint::ParseIntervals("[1.0,2.0); (3.1.4,] ; [,0.5]");
  CHECK(ivs.size() == 3);
  CHECK(ivs[0].lo && ivs[0].lo->ToString() == "1.0" && ivs[0].lo_inclusive);
  CHECK(ivs[0].hi && ivs[0].hi->ToString() == "2.0" && !ivs[0].hi_inclusive);
  CHECK(ivs[1].lo && !ivs[1].lo_inclusive && !ivs[1].hi);
  CHECK(!ivs[2].lo && ivs[2].hi && ivs[2].hi_inclusive);

  CHECK_THROWS(VersionConstraint::ParseIntervals(""));
  CHECK_THROWS(VersionConstraint::ParseIntervals("1.0,2.0"));
  CHECK_THROWS(VersionConstraint::ParseIntervals("[1.0;2.0]"));
  CHECK_THROWS(VersionConstraint::ParseIntervals("[1.0,2.0,3.0]"));
  CHECK_THROWS(VersionConstraint::ParseIntervals("[x,2.0]"));
}

TEST(version_constraint_matches) {
  auto c = VersionConstraint::Parse("app", "[1.0,2.0); (3.0,]");
  Request req;
  auto check = [&](const char* ver, bool expected) {
    req.versions["app"] = *Version::Parse(ver);
    CHECK(c->Matches(req) == expected);
  };
  check("1.0", true);
  check("1.0.0.0", true);
  check("1.9.9", true);
  check("2.0", false);   // исключающая граница
  check("2.0.0", false);
  check("2.5", false);
  check("3.0", false);   // исключающая нижняя
  check("3.0.1", true);
  check("100", true);
  check("0.9", false);

  CHECK(!c->Matches(Request{}));  // нет свойства
}

TEST(domain_pattern_parse) {
  auto p = DomainConstraint::ParsePattern("*.Example.{tld}");
  CHECK(p.star && p.tld_wildcard);
  CHECK(p.suffix == (std::vector<std::string>{"example"}));

  auto q = DomainConstraint::ParsePattern("mail.example.ru");
  CHECK(!q.star && !q.tld_wildcard);
  CHECK(q.suffix == (std::vector<std::string>{"ru", "example", "mail"}));

  CHECK_THROWS(DomainConstraint::ParsePattern(""));
  CHECK_THROWS(DomainConstraint::ParsePattern("*.{tld}"));      // нет фиксированных меток
  CHECK_THROWS(DomainConstraint::ParsePattern("a.*.b"));        // '*' не слева
  CHECK_THROWS(DomainConstraint::ParsePattern("{tld}.a.b"));    // '{tld}' не справа
  CHECK_THROWS(DomainConstraint::ParsePattern("a..b"));         // пустая метка
}

TEST(domain_constraint_matches) {
  DomainConstraint c("host", {"*.example.{tld}", "mail.direct.ru"});
  Request req;
  auto check = [&](const char* host, bool expected) {
    req.domains["host"] = host;
    CHECK(c.Matches(req) == expected);
  };
  check("example.ru", true);       // '*' покрывает и ноль поддоменов
  check("example.com", true);
  check("www.example.ru", true);
  check("a.b.c.example.co", true);
  check("WWW.EXAMPLE.RU", true);   // регистронезависимо
  check("www.example.ru.", true);  // FQDN
  check("badexample.ru", false);
  check("example", false);         // нет TLD-метки
  check("ru.example", false);
  check("mail.direct.ru", true);
  check("smtp.mail.direct.ru", false);  // без '*' — точное совпадение
  check("mail.direct.com", false);      // фиксированный tld
  check("", false);

  CHECK(!c.Matches(Request{}));
}

namespace {

// 1 (мир) <- 2 (Европа) <- 3 (Россия) <- 4 (Москва); 5 (США) <- 1.
std::shared_ptr<RegionTree> MakeTestTree() {
  auto tree = std::make_shared<RegionTree>();
  tree->AddRegion(1, 1);  // корень
  tree->AddRegion(2, 1);
  tree->AddRegion(3, 2);
  tree->AddRegion(4, 3);
  tree->AddRegion(5, 1);
  tree->AddIpRange("10.0.0.0", "10.0.0.255", 4);
  tree->AddIpRange("10.0.1.0", "10.0.1.255", 5);
  tree->AddIpRange("2001:db8::", "2001:db8::ffff", 2);
  tree->Build();
  return tree;
}

}  // namespace

TEST(region_tree) {
  auto tree = MakeTestTree();
  CHECK(!tree->Parent(1).has_value());
  CHECK(tree->Parent(4) == 3u);

  CHECK(tree->Contains(1, 4));   // Москва внутри мира
  CHECK(tree->Contains(2, 4));   // и внутри Европы
  CHECK(tree->Contains(4, 4));   // регион входит сам в себя
  CHECK(!tree->Contains(5, 4));  // но не внутри США
  CHECK(!tree->Contains(4, 2));  // Европа не внутри Москвы
  CHECK(!tree->Contains(1, 99)); // неизвестный регион — только сам в себя
  CHECK(tree->Contains(99, 99));

  CHECK(tree->RegionByIp("10.0.0.42") == 4u);
  CHECK(tree->RegionByIp("10.0.1.7") == 5u);
  CHECK(tree->RegionByIp("::ffff:10.0.0.42") == 4u);  // IPv4-mapped запись
  CHECK(tree->RegionByIp("2001:db8::abcd") == 2u);
  CHECK(!tree->RegionByIp("10.0.2.1").has_value());   // вне диапазонов
  CHECK(!tree->RegionByIp("999.1.1.1").has_value());  // некорректный IP
  CHECK(!tree->RegionByIp("garbage").has_value());
  CHECK(!tree->RegionByIp("").has_value());

  RegionTree dup;
  dup.AddRegion(1, 1);
  CHECK_THROWS(dup.AddRegion(1, 1));

  RegionTree overlapping;
  overlapping.AddIpRange("10.0.0.0", "10.0.0.100", 1);
  overlapping.AddIpRange("10.0.0.100", "10.0.0.200", 2);
  CHECK_THROWS(overlapping.Build());

  RegionTree cyclic;
  cyclic.AddRegion(7, 8);
  cyclic.AddRegion(8, 7);
  CHECK_THROWS(cyclic.Build());

  RegionTree bad;
  CHECK_THROWS(bad.AddIpRange("10.0.0.5", "10.0.0.1", 1));  // from > to
  CHECK_THROWS(bad.AddIpRange("nope", "10.0.0.1", 1));
}

TEST(region_constraint) {
  auto tree = MakeTestTree();
  RegionConstraint in_europe("geo", tree, {2});
  RegionConstraint not_in_europe("geo", tree, {2}, /*negated=*/true);

  Request req;
  req.regions["geo"] = 4;  // Москва — внутри Европы по цепочке предков
  CHECK(in_europe.Matches(req));
  CHECK(!not_in_europe.Matches(req));

  req.regions["geo"] = 5;  // США
  CHECK(!in_europe.Matches(req));
  CHECK(not_in_europe.Matches(req));

  req.regions["geo"] = 2;  // сама Европа
  CHECK(in_europe.Matches(req));

  // Неизвестный дереву регион: определён, но никуда не входит.
  req.regions["geo"] = 99;
  CHECK(!in_europe.Matches(req));
  CHECK(not_in_europe.Matches(req));

  // Регион не определён — не выполнено в обоих режимах.
  Request empty;
  CHECK(!in_europe.Matches(empty));
  CHECK(!not_in_europe.Matches(empty));

  // Определение региона по IP при отсутствии явного id.
  Request by_ip;
  by_ip.ip = "10.0.0.42";  // Москва
  CHECK(in_europe.Matches(by_ip));
  by_ip.ip = "10.0.1.7";  // США
  CHECK(!in_europe.Matches(by_ip));
  by_ip.ip = "10.0.2.1";  // вне диапазонов — регион не определён
  CHECK(!in_europe.Matches(by_ip));
  CHECK(!not_in_europe.Matches(by_ip));

  // Явный id имеет приоритет над IP.
  Request both;
  both.regions["geo"] = 5;
  both.ip = "10.0.0.42";
  CHECK(!in_europe.Matches(both));

  CHECK_THROWS(RegionConstraint("geo", tree, {}));
  CHECK_THROWS(RegionConstraint("geo", nullptr, {1}));
}

TEST(xxh64_reference_vectors) {
  CHECK(XXH64("", 0, 0) == 0xEF46DB3751D8E999ULL);
  CHECK(XXH64("abc", 3, 0) == 0x44BC2CF5AD770999ULL);
}

TEST(flatten_validation) {
  auto make_exp = [](std::string id, std::vector<uint32_t> slots) {
    ExperimentConfig e;
    e.id = std::move(id);
    e.id_key = "uid";
    e.salt = "s";
    e.total_buckets = 100;
    e.groups = {{"control", 50}, {"exp", 50}};
    e.slots = std::move(slots);
    return e;
  };

  Config ok;
  DimensionConfig dim;
  dim.id = "d1";
  dim.id_key = "uid";
  dim.salt = "dsalt";
  dim.total_slots = 10;
  dim.experiments = {make_exp("e1", {0, 1}), make_exp("e2", {2})};
  ok.dimensions.push_back(dim);
  ok.experiments.push_back(make_exp("free", {}));
  auto flat = Flatten(ok);
  CHECK(flat.size() == 3);
  CHECK(flat[0].slot_checks.size() == 1 && flat[0].dimension_id == "d1");
  CHECK(flat[0].slot_checks[0].total_slots == 10);
  CHECK(flat[2].slot_checks.empty());
  CHECK(flat[0].groups.size() == 2);
  CHECK(flat[0].groups[0].begin == 0 && flat[0].groups[0].end == 50);
  CHECK(flat[0].groups[1].begin == 50 && flat[0].groups[1].end == 100);
  CHECK(flat[0].restrictions.empty());  // ограничений не задано

  // Пересечение слотов.
  Config bad = ok;
  bad.dimensions[0].experiments.push_back(make_exp("e3", {1}));
  CHECK_THROWS(Flatten(bad));

  // Слот за пределами измерения.
  Config bad2 = ok;
  bad2.dimensions[0].experiments.push_back(make_exp("e3", {10}));
  CHECK_THROWS(Flatten(bad2));

  // Переполнение бакетов.
  Config bad3 = ok;
  bad3.experiments[0].groups = {{"a", 80}, {"b", 30}};
  CHECK_THROWS(Flatten(bad3));
}

TEST(end_to_end_smoke) {
  // Небольшой сценарий целиком: измерение + свободный эксперимент.
  Config config;
  DimensionConfig dim;
  dim.id = "ui";
  dim.id_key = "uid";
  dim.salt = "ui-salt";
  dim.total_slots = 100;
  dim.constraints = {
      std::make_shared<StringConstraint>("service",
                                         std::vector<std::string>{"web"})};

  ExperimentConfig e1;
  e1.id = "new-header";
  e1.id_key = "uid";
  e1.salt = "hdr";
  e1.total_buckets = 100;
  e1.groups = {{"control", 50}, {"treatment", 50}};
  for (uint32_t s = 0; s < 50; ++s) e1.slots.push_back(s);
  e1.constraints = {VersionConstraint::Parse("app_version", "[2.0,]")};
  dim.experiments.push_back(e1);
  config.dimensions.push_back(dim);

  auto hashers = HasherRegistry::CreateDefault();
  auto flat = Flatten(config);
  NaiveMatcher naive(flat, hashers);
  IndexedMatcher indexed(flat, hashers);

  int assigned = 0;
  for (int u = 0; u < 1000; ++u) {
    Request req;
    req.strings["service"] = "web";
    req.versions["app_version"] = *Version::Parse("2.1.0");
    req.ids["uid"] = "user-" + std::to_string(u);
    auto a = naive.Match(req);
    auto b = indexed.Match(req);
    CHECK(a.size() == b.size());
    if (!a.empty()) {
      ++assigned;
      CHECK(a[0].ExperimentId() == "new-header");
      CHECK(a[0].GroupName() == b[0].GroupName());
    }
  }
  // Эксперимент занимает половину слотов — назначена примерно половина.
  CHECK(assigned > 350 && assigned < 650);

  Request wrong_service;
  wrong_service.strings["service"] = "ios";
  wrong_service.versions["app_version"] = *Version::Parse("2.1");
  wrong_service.ids["uid"] = "user-1";
  CHECK(naive.Match(wrong_service).empty());
  CHECK(indexed.Match(wrong_service).empty());
}

TEST_MAIN()
