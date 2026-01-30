idf_component_register(
    SRCS
        "main.cpp"
        "wifi_ap.cpp"
        "http_server.cpp"
        "websocket_server.cpp"
        "uart_bridge.cpp"
        "../../common/protocol.cpp"
        "../../common/uart_bridge_base.cpp"
    INCLUDE_DIRS
        "."
        "../../common"
    PRIV_REQUIRES
        esp_wifi
        esp_http_server
        nvs_flash
        driver
        freertos
        json
)

# C++23 для всей прошивки (явно закрепляем стандарт)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++23)

