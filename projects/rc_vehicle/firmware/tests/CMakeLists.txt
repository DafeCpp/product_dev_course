cmake_minimum_required(VERSION 3.16)
project(rc_vehicle_tests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable testing
enable_testing()

# Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Common source files from the firmware
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# Common sources that don't depend on platform
set(COMMON_SOURCES
    ${COMMON_DIR}/protocol.cpp
    ${COMMON_DIR}/madgwick_filter.cpp
    ${COMMON_DIR}/failsafe.cpp
    ${COMMON_DIR}/lpf_butterworth.cpp
    ${COMMON_DIR}/imu_calibration.cpp
    ${COMMON_DIR}/control_components.cpp
    ${COMMON_DIR}/uart_bridge_base.cpp
    ${COMMON_DIR}/pid_controller.cpp
)

# Include directories
include_directories(
    ${COMMON_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Windows: define _USE_MATH_DEFINES so M_PI and friends are available
if(WIN32)
    add_compile_definitions(_USE_MATH_DEFINES)
endif()

# Unit tests executable
add_executable(unit_tests
    ${COMMON_SOURCES}
    unit/test_protocol.cpp
    unit/test_madgwick.cpp
    unit/test_failsafe.cpp
    unit/test_lpf.cpp
    unit/test_pid.cpp
    unit/test_stabilization_config.cpp
)

target_link_libraries(unit_tests
    gtest
    gtest_main
    gmock
)

# Discover tests
gtest_discover_tests(unit_tests)

# Integration tests executable
add_executable(integration_tests
    ${COMMON_SOURCES}
    integration/test_control_loop.cpp
)

target_link_libraries(integration_tests
    gtest
    gtest_main
    gmock
)

gtest_discover_tests(integration_tests)

# Coverage support (optional)
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(unit_tests PRIVATE --coverage)
        target_link_options(unit_tests PRIVATE --coverage)
        target_compile_options(integration_tests PRIVATE --coverage)
        target_link_options(integration_tests PRIVATE --coverage)
    endif()
endif()

# Print configuration
message(STATUS "RC Vehicle Tests Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Coverage: ${ENABLE_COVERAGE}")
