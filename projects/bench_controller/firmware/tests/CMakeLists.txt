cmake_minimum_required(VERSION 3.16)
project(bench_controller_tests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Портируемые исходники — единый список (см. ../sources.cmake)
set(BENCH_FIRMWARE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
include(${BENCH_FIRMWARE_DIR}/sources.cmake)

# --- unit + integration тесты ---
file(GLOB UNIT_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)
file(GLOB INTEGRATION_TEST_SOURCES
     ${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cpp)

add_executable(unit_tests
    ${BENCH_PORTABLE_SOURCES}
    ${UNIT_TEST_SOURCES}
    ${INTEGRATION_TEST_SOURCES}
)
target_include_directories(unit_tests PRIVATE
    ${BENCH_PORTABLE_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(unit_tests PRIVATE gtest gtest_main gmock)
gtest_discover_tests(unit_tests)

# --- sim_host: ядро прошивки как CLI (SIL) ---
add_executable(sim_host
    ${BENCH_PORTABLE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/sim/sim_host_main.cpp
)
target_include_directories(sim_host PRIVATE
    ${BENCH_PORTABLE_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
