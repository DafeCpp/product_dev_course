# Единый список портируемых исходников (common + CANopen) — см.
# firmware/sources.cmake, чтобы не дублировать список (урок rc_vehicle).
set(BENCH_FIRMWARE_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${BENCH_FIRMWARE_DIR}/sources.cmake)

idf_component_register(
    SRCS
        # esp32-специфика
        "main.cpp"
        "bench_platform_esp32.cpp"
        "twai_can_bus.cpp"
        "valve_emulator.cpp"
        # портируемое ядро + CANopen-слой
        ${BENCH_PORTABLE_SOURCES}
        ${BENCH_CANOPEN_C_SOURCES}
        ${BENCH_CANOPEN_CXX_SOURCES}
    INCLUDE_DIRS
        "."
        ${BENCH_PORTABLE_INCLUDE_DIRS}
        ${BENCH_CANOPEN_INCLUDE_DIRS}
    PRIV_REQUIRES
        esp_driver_twai
        esp_timer
        esp_system
        freertos
        log
)

# CANopenNode — C99-код апстрима; глушим его предупреждения, не свои.
set_source_files_properties(${BENCH_CANOPEN_C_SOURCES}
    TARGET_DIRECTORY ${COMPONENT_LIB}
    PROPERTIES COMPILE_OPTIONS "-w")

# C++23 для нашего кода (std::expected и пр.)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 23)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_EXTENSIONS ON)
