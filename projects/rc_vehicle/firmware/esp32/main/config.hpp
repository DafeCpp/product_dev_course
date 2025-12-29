#pragma once

// Wi-Fi конфигурация
#define WIFI_AP_SSID_PREFIX "RC-Vehicle"
#define WIFI_AP_PASSWORD "" // Пустой пароль для MVP (можно добавить позже)
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_MAX_CONNECTIONS 4

// HTTP сервер
#define HTTP_SERVER_PORT 80

// WebSocket сервер
#define WEBSOCKET_SERVER_PORT 81
#define WEBSOCKET_MAX_CLIENTS 4

// UART конфигурация (ESP32 ↔ RP2040)
#define UART_PORT_NUM UART_NUM_1
#define UART_BAUD_RATE 115200
#define UART_TX_PIN GPIO_NUM_17 // TX на ESP32-S3 Zero mini
#define UART_RX_PIN GPIO_NUM_18 // RX на ESP32-S3 Zero mini
#define UART_BUF_SIZE 1024

// Тайминги (в миллисекундах)
#define COMMAND_SEND_INTERVAL_MS 20 // 50 Hz - частота отправки команд на RP2040
#define TELEM_SEND_INTERVAL_MS                                                 \
  50 // 20 Hz - частота отправки телеметрии в браузер
#define UART_RESPONSE_TIMEOUT_MS 100 // Таймаут ожидания ответа от RP2040

// Протокол UART
#define UART_FRAME_PREFIX_0 0xAA
#define UART_FRAME_PREFIX_1 0x55
#define UART_PROTOCOL_VERSION 0x01

// Типы сообщений UART
#define UART_MSG_TYPE_COMMAND 0x01
#define UART_MSG_TYPE_TELEM 0x02
#define UART_MSG_TYPE_PING 0x03
#define UART_MSG_TYPE_PONG 0x04

// Размеры буферов
#define WS_RX_BUFFER_SIZE 512
#define WS_TX_BUFFER_SIZE 512
#define UART_RX_BUFFER_SIZE 1024
#define UART_TX_BUFFER_SIZE 512

// Отладка
#define DEBUG_ENABLED 1
#define DEBUG_UART_NUM UART_NUM_0 // USB Serial для отладки
