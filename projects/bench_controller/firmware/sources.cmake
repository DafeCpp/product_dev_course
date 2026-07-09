# Единый список портируемых исходников bench_controller.
# Подключается всеми потребителями (host-тесты, ESP-IDF, позже STM32),
# чтобы список не дублировался (урок rc_vehicle, где он задвоен).
#
# Использование:
#   set(BENCH_FIRMWARE_DIR <путь к firmware/>)
#   include(${BENCH_FIRMWARE_DIR}/sources.cmake)
#   → переменные BENCH_PORTABLE_SOURCES, BENCH_PORTABLE_INCLUDE_DIRS

set(BENCH_COMMON_DIR ${BENCH_FIRMWARE_DIR}/common)

set(BENCH_PORTABLE_SOURCES
    ${BENCH_COMMON_DIR}/pid_controller.cpp
    ${BENCH_COMMON_DIR}/sine_program.cpp
    ${BENCH_COMMON_DIR}/specimen_failure_detector.cpp
    ${BENCH_COMMON_DIR}/link_watchdog.cpp
    ${BENCH_COMMON_DIR}/channel_controller.cpp
    ${BENCH_COMMON_DIR}/hydraulic_plant_model.cpp
    ${BENCH_COMMON_DIR}/bench_control_loop.cpp
)

set(BENCH_PORTABLE_INCLUDE_DIRS
    ${BENCH_COMMON_DIR}
)
