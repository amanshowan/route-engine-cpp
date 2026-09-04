cmake_minimum_required(VERSION 3.21)

project(route_engine
        VERSION 1.0.0
        DESCRIPTION "C++ route and network engine"
        LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ROUTE_BUILD_TESTS "Build the route-engine unit tests" ${PROJECT_IS_TOP_LEVEL})
option(ROUTE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Warning policy
#
# Carried by an INTERFACE target and linked PRIVATE into each of our own
# targets, so the flags never leak into a consumer or into GoogleTest. Warnings
# are not errors by default; continuous integration is expected to configure
# with -DROUTE_WARNINGS_AS_ERRORS=ON.
# ---------------------------------------------------------------------------
add_library(route_warnings INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
  target_compile_options(route_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion)
  if(ROUTE_WARNINGS_AS_ERRORS)
    target_compile_options(route_warnings INTERFACE -Werror)
  endif()
else()
  message(WARNING
    "route-engine: no warning flags are defined for compiler "
    "'${CMAKE_CXX_COMPILER_ID}'; building without the project warning policy.")
endif()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(route_core
  src/benchmark.cpp
  src/builder.cpp
  src/csv.cpp
  src/generator.cpp
  src/graph.cpp
  src/metric.cpp
  src/search.cpp)

target_include_directories(route_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(route_core PUBLIC cxx_std_20)
target_link_libraries(route_core PRIVATE route_warnings)

# ---------------------------------------------------------------------------
# Command-line interface (stub in this commit)
# ---------------------------------------------------------------------------
add_executable(route_cli src/cli/main.cpp)
set_target_properties(route_cli PROPERTIES OUTPUT_NAME route-engine)
target_link_libraries(route_cli PRIVATE route_core route_warnings)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(ROUTE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
