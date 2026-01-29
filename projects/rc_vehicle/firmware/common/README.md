# Общий код прошивок RC Vehicle

Папка `firmware/common/` содержит код, общий для ESP32, RP2040 и STM32.

## Содержимое

- **protocol.hpp / protocol.cpp** — протокол UART (ESP32 ↔ MCU): структура `TelemetryData`, константы (AA 55, типы сообщений), `ProtocolBuildTelem`, `ProtocolParseCommand`, `ProtocolBuildCommand`, `ProtocolParseTelem`, CRC16, поиск начала кадра.
- **uart_bridge_base.hpp** — абстрактный базовый класс UART-моста:
  - чисто виртуальные: `Init()`, `Write(data, len)`, `ReadAvailable(buf, max_len)`;
  - реализованы в базе (через протокол и буфер приёма): `SendTelem`, `ReceiveCommand` (для MCU), `SendCommand`, `ReceiveTelem` (для ESP32).

Наследники по платформам:
- **RP2040:** `Rp2040UartBridge` в `rp2040/main/uart_bridge.cpp` (Pico SDK: uart_*, gpio_*).
- **STM32:** `Stm32UartBridge` в `stm32/main/uart_bridge.cpp` (libopencm3, пока заглушки).
- **ESP32:** `Esp32UartBridge` в `esp32/main/uart_bridge.cpp` (ESP-IDF: uart_driver_install, uart_write_bytes, uart_read_bytes).

Везде сохраняется прежний C-API (`UartBridgeInit`, `UartBridgeSendTelem` / `UartBridgeSendCommand` и т.д.), внутри создаётся один экземпляр наследника и вызываются методы базового класса.

## Подключение

- **RP2040:** `main/CMakeLists.txt` — `RC_VEHICLE_COMMON_DIR`, include, `protocol.cpp`.
- **STM32:** корневой `CMakeLists.txt` — `RC_VEHICLE_COMMON_DIR`, include, `protocol.cpp`.
- **ESP32:** `main/CMakeLists.txt` — `INCLUDE_DIRS "../../common"`, `SRCS "../../common/protocol.cpp"`.
