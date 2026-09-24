cmake_minimum_required(VERSION 3.16)
project(tinysearch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(TINYSEARCH_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

add_compile_options(-Wall -Wextra -Wpedantic)
if(TINYSEARCH_WARNINGS_AS_ERRORS)
  add_compile_options(-Werror)
endif()

find_package(Threads REQUIRED)

# --- core library ------------------------------------------------------

add_library(tinysearch_core
  src/tokenizer.cpp
  src/normalizer.cpp
  src/index.cpp
  src/parser.cpp
  src/scorer.cpp
  src/snippet.cpp
  src/config.cpp
  src/index_store.cpp
  src/search_engine.cpp
  src/http_server.cpp
)
target_include_directories(tinysearch_core PUBLIC include)
target_link_libraries(tinysearch_core PUBLIC Threads::Threads)

# --- CLI / server binary -------------------------------------------------

add_executable(tinysearch src/main.cpp)
target_link_libraries(tinysearch PRIVATE tinysearch_core)

# --- tests ---------------------------------------------------------------

enable_testing()
add_executable(tinysearch_tests
  tests/test_main.cpp
  tests/tokenizer_test.cpp
  tests/normalizer_test.cpp
  tests/index_test.cpp
  tests/parser_test.cpp
  tests/bm25_test.cpp
  tests/cache_test.cpp
  tests/persistence_test.cpp
  tests/search_test.cpp
)
target_include_directories(tinysearch_tests PRIVATE tests)
target_link_libraries(tinysearch_tests PRIVATE tinysearch_core)
add_test(NAME tinysearch_tests COMMAND tinysearch_tests)

# --- benchmarks ------------------------------------------------------------

add_executable(indexing_benchmark benchmark/indexing_benchmark.cpp)
target_link_libraries(indexing_benchmark PRIVATE tinysearch_core)

add_executable(search_benchmark benchmark/search_benchmark.cpp)
target_link_libraries(search_benchmark PRIVATE tinysearch_core)

add_executable(ranking_evaluation benchmark/ranking_evaluation.cpp)
target_link_libraries(ranking_evaluation PRIVATE tinysearch_core)
