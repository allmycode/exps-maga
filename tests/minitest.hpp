#pragma once

// Минимальный тестовый фреймворк без внешних зависимостей.

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace minitest {

struct Registry {
  static Registry& Instance() {
    static Registry r;
    return r;
  }
  std::vector<std::pair<std::string, std::function<void()>>> tests;
  int failures = 0;
};

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    Registry::Instance().tests.emplace_back(name, std::move(fn));
  }
};

inline int RunAll() {
  auto& reg = Registry::Instance();
  for (auto& [name, fn] : reg.tests) {
    int before = reg.failures;
    fn();
    std::printf("[%s] %s\n", reg.failures == before ? " OK " : "FAIL",
                name.c_str());
  }
  if (reg.failures) {
    std::printf("%d check(s) FAILED\n", reg.failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}

}  // namespace minitest

#define TEST(name)                                                        \
  static void test_##name();                                              \
  static ::minitest::Registrar registrar_##name(#name, test_##name);      \
  static void test_##name()

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      ++::minitest::Registry::Instance().failures;                        \
      std::printf("  CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                     \
  } while (0)

#define CHECK_THROWS(expr)                                                \
  do {                                                                    \
    bool thrown = false;                                                  \
    try {                                                                 \
      (void)(expr);                                                       \
    } catch (const std::exception&) {                                     \
      thrown = true;                                                      \
    }                                                                     \
    CHECK(thrown);                                                        \
  } while (0)

#define TEST_MAIN() \
  int main() { return ::minitest::RunAll(); }
