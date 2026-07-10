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

# --- CANopen-слой (портируемый: hosted-драйвер поверх ICanBus) ---
# CANopenNode — C99; компилируется как C. SocketCanBus (Linux-only)
# сюда не входит — его добавляет host-сборка отдельно.
set(BENCH_CANOPEN_DIR ${BENCH_FIRMWARE_DIR}/canopen)
set(BENCH_CANOPEN_THIRD_PARTY ${BENCH_CANOPEN_DIR}/third_party/CANopenNode)

set(BENCH_CANOPEN_C_SOURCES
    ${BENCH_CANOPEN_THIRD_PARTY}/CANopen.c
    ${BENCH_CANOPEN_THIRD_PARTY}/301/CO_ODinterface.c
    ${BENCH_CANOPEN_THIRD_PARTY}/301/CO_NMT_Heartbeat.c
    ${BENCH_CANOPEN_THIRD_PARTY}/301/CO_HBconsumer.c
    ${BENCH_CANOPEN_THIRD_PARTY}/301/CO_Emergency.c
    ${BENCH_CANOPEN_THIRD_PARTY}/301/CO_PDO.c
    ${BENCH_CANOPEN_THIRD_PARTY}/301/CO_SDOserver.c
    ${BENCH_CANOPEN_THIRD_PARTY}/301/CO_SDOclient.c
    ${BENCH_CANOPEN_THIRD_PARTY}/301/CO_SYNC.c
    ${BENCH_CANOPEN_THIRD_PARTY}/301/CO_fifo.c
    ${BENCH_CANOPEN_THIRD_PARTY}/301/crc16-ccitt.c
    ${BENCH_CANOPEN_DIR}/od/OD.c
    ${BENCH_CANOPEN_DIR}/co_driver_hosted.c
)

set(BENCH_CANOPEN_CXX_SOURCES
    ${BENCH_CANOPEN_DIR}/co_master.cpp
    ${BENCH_CANOPEN_DIR}/canopen_valve_channel.cpp
)

# Порядок важен: canopen/ (наш co_driver_target.h и od/OD.h) раньше
# каталога апстрима
set(BENCH_CANOPEN_INCLUDE_DIRS
    ${BENCH_CANOPEN_DIR}
    ${BENCH_CANOPEN_DIR}/od
    ${BENCH_CANOPEN_THIRD_PARTY}
)
