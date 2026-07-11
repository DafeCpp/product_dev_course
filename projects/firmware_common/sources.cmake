# Platform-independent control primitives shared by firmware targets.
# Consumers include this file after setting their own project configuration.

set(FIRMWARE_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})

set(FIRMWARE_COMMON_SOURCES
    ${FIRMWARE_COMMON_DIR}/src/pid_controller.cpp
)

set(FIRMWARE_COMMON_INCLUDE_DIRS
    ${FIRMWARE_COMMON_DIR}/include
)
