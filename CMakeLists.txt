cmake_minimum_required(VERSION 3.16)
project(heliospan LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)

set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_SHALLOW TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_library(heliospan_core
  src/domain.cpp
  src/power.cpp
  src/rules.cpp
  src/seed.cpp
  src/simulator.cpp
  src/store.cpp
  src/json_codec.cpp
  src/server.cpp
)
target_include_directories(heliospan_core
  PUBLIC include
  PRIVATE src
)
target_link_libraries(heliospan_core PUBLIC nlohmann_json::nlohmann_json httplib::httplib)
target_compile_options(heliospan_core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(heliospan_core PUBLIC HELIOSPAN_WEB_DIR="${CMAKE_SOURCE_DIR}/web")

add_executable(heliospan src/main.cpp)
target_link_libraries(heliospan PRIVATE heliospan_core)
target_compile_options(heliospan PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(heliospan_tests
  tests/test_rules.cpp
  tests/test_simulator.cpp
  tests/test_api.cpp
)
target_link_libraries(heliospan_tests PRIVATE heliospan_core GTest::gtest_main)
target_compile_options(heliospan_tests PRIVATE -Wall -Wextra -Wpedantic)
include(GoogleTest)
gtest_discover_tests(heliospan_tests DISCOVERY_MODE PRE_TEST)
