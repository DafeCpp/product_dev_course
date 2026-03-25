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

FetchContent_Declare(
  cjson
  GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
  GIT_TAG v1.7.18
)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cjson)

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
    ${COMMON_DIR}/vehicle_ekf.cpp
    ${COMMON_DIR}/telemetry_log.cpp
    ${COMMON_DIR}/stabilization_config.cpp
    ${COMMON_DIR}/stabilization_pipeline.cpp
    ${COMMON_DIR}/kids_mode_processor.cpp
    ${COMMON_DIR}/self_test.cpp
    ${COMMON_DIR}/drive_modes.cpp
    ${COMMON_DIR}/drive_mode_registry.cpp
    ${COMMON_DIR}/calibration_manager.cpp
    ${COMMON_DIR}/stabilization_manager.cpp
    ${COMMON_DIR}/telemetry_manager.cpp
    ${COMMON_DIR}/vehicle_control_unified.cpp
    ${COMMON_DIR}/steering_trim_calibration.cpp
    ${COMMON_DIR}/test_runner.cpp
    ${COMMON_DIR}/com_offset_calibration.cpp
)

# Include directories
include_directories(
    ${COMMON_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    ${cjson_SOURCE_DIR}
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
    unit/test_vehicle_ekf.cpp
    unit/test_telemetry_log.cpp
    unit/test_oversteer_guard.cpp
    unit/test_kids_mode.cpp
    unit/test_self_test.cpp
    unit/test_yaw_rate_controller.cpp
    unit/test_pitch_compensator.cpp
    unit/test_slip_angle_controller.cpp
    unit/test_slew_rate.cpp
    unit/test_control_source.cpp
    unit/test_telemetry_handler.cpp
    unit/test_drive_modes.cpp
    unit/test_telemetry_manager.cpp
    unit/test_calibration_manager.cpp
    unit/test_stabilization_manager.cpp
    unit/test_steering_trim_calibration.cpp
    unit/test_test_runner.cpp
    unit/test_com_offset_calibration.cpp
    unit/test_com_offset_correction.cpp
    integration/test_control_loop.cpp
)

target_link_libraries(unit_tests
    gtest
    gtest_main
    gmock
    cjson
)

# Discover tests
gtest_discover_tests(unit_tests)

# Coverage support (optional)
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(unit_tests PRIVATE --coverage)
        target_link_options(unit_tests PRIVATE --coverage)
    endif()
endif()

# Print configuration
message(STATUS "RC Vehicle Tests Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Coverage: ${ENABLE_COVERAGE}")
