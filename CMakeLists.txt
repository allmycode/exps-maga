cmake_minimum_required(VERSION 3.16)
project(expassign CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(expassign
  src/version.cpp
  src/hash.cpp
  src/string_constraint.cpp
  src/bool_constraint.cpp
  src/version_constraint.cpp
  src/domain_constraint.cpp
  src/region_tree.cpp
  src/region_constraint.cpp
  src/flatten.cpp
  src/naive_matcher.cpp
  src/indexed_matcher.cpp
)
target_include_directories(expassign PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(expassign PRIVATE -Wall -Wextra)

enable_testing()

add_executable(test_constraints tests/test_constraints.cpp)
target_link_libraries(test_constraints PRIVATE expassign)
add_test(NAME constraints COMMAND test_constraints)

add_executable(test_matchers_equivalence tests/test_matchers_equivalence.cpp)
target_link_libraries(test_matchers_equivalence PRIVATE expassign)
add_test(NAME matchers_equivalence COMMAND test_matchers_equivalence)

add_executable(expassign_bench bench/bench.cpp)
target_link_libraries(expassign_bench PRIVATE expassign)

add_executable(expassign_example examples/example.cpp)
target_link_libraries(expassign_example PRIVATE expassign)
