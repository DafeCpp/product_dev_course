# ESP32-специфичная веб-инфраструктура, переиспользуемая ESP-IDF-таргетами
# (rc_vehicle, bench-head). Host-тестов у этого кода нет — он весь на esp-idf
# API, поэтому подключается только ESP-IDF-компонентами через
# idf_component_register(), а не CMake-таргетами host-тестов.
#
# Использование (в CMakeLists.txt ESP-IDF main-компонента):
#   include(<путь>/firmware_common/esp32/sources_esp32.cmake)
#   → переменные FIRMWARE_COMMON_ESP32_SOURCES, FIRMWARE_COMMON_ESP32_INCLUDE_DIRS

set(FIRMWARE_COMMON_ESP32_DIR ${CMAKE_CURRENT_LIST_DIR})

set(FIRMWARE_COMMON_ESP32_SOURCES
    ${FIRMWARE_COMMON_ESP32_DIR}/src/wifi_ap.cpp
    ${FIRMWARE_COMMON_ESP32_DIR}/src/dns_server.cpp
    ${FIRMWARE_COMMON_ESP32_DIR}/src/http_server.cpp
    ${FIRMWARE_COMMON_ESP32_DIR}/src/websocket_server.cpp
)

set(FIRMWARE_COMMON_ESP32_INCLUDE_DIRS
    ${FIRMWARE_COMMON_ESP32_DIR}/include
)
