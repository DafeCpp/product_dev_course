cmake_minimum_required(VERSION 3.16)
project(firmware_common_tests CXX)

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
include(${CMAKE_CURRENT_SOURCE_DIR}/../sources.cmake)

add_executable(firmware_common_tests
    ${FIRMWARE_COMMON_SOURCES}
    test_pid_controller.cpp
    test_slew_rate.cpp
)
target_include_directories(firmware_common_tests PRIVATE
    ${FIRMWARE_COMMON_INCLUDE_DIRS}
)
target_link_libraries(firmware_common_tests PRIVATE gtest_main)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(firmware_common_tests PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
endif()

gtest_discover_tests(firmware_common_tests)
