include(FetchContent)

# GoogleTest v1.17.0, pinned to the exact upstream commit that the tag points
# at rather than to the moving tag itself. A full (non-shallow) clone is
# required because an arbitrary commit cannot be fetched shallowly from every
# server configuration.
set(ROUTE_GOOGLETEST_REVISION "52eb8108c5bdec04579160ae17225d66034bd723")

FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        "${ROUTE_GOOGLETEST_REVISION}"
  GIT_SHALLOW    OFF)

# GoogleMock is not used, and GoogleTest must not be installed with us.
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(route_tests
  test_benchmark.cpp
  test_csv.cpp
  test_generator.cpp
  test_graph.cpp
  test_metric.cpp
  test_search.cpp)

target_include_directories(route_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(route_tests PRIVATE route_core route_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(route_tests)
