# Общий код прошивок RC Vehicle

Папка `firmware/common/` содержит код, общий для ESP32, RP2040 и STM32. Используется C++23; стиль кода и правила по буферам описаны в [README корня firmware](../README.md).

## Содержимое

- **protocol.hpp / protocol.cpp** — протокол UART (ESP32 ↔ MCU): структура `TelemetryData`, константы (AA 55, типы сообщений), `ProtocolBuildTelem`, `ProtocolParseCommand`, `ProtocolBuildCommand`, `ProtocolParseTelem`, CRC16, поиск начала кадра.
- **uart_bridge_base.hpp** — абстрактный базовый класс UART-моста:
  - чисто виртуальные: `Init()`, `Write(data, len)`, `ReadAvailable(buf, max_len)`;
  - реализованы в базе (через протокол и буфер приёма): `SendTelem`, `ReceiveCommand` (для MCU), `SendCommand`, `ReceiveTelem` (для ESP32).
- **spi_base.hpp** — абстрактный базовый класс SPI-драйвера:
  - чисто виртуальные: `Init()`, `Transfer(tx, rx, len)` (полнодуплексный обмен; реализация держит CS на время обмена).
- **mpu6050_spi.hpp / mpu6050_spi.cpp** — драйвер MPU-6050 по SPI: структура `ImuData`, класс `Mpu6050Spi(SpiBase*)` с `Init()`, `Read(ImuData*)`, `ConvertToTelem(...)`.
- **rc_vehicle_common.hpp** — утилиты PWM/RC: `rc_vehicle::PulseWidthUsFromNormalized(value, min_us, neutral_us, max_us)`, `NormalizedFromPulseWidthUs(pulse_us, ...)`, `ClampNormalized(value)`. Используются в pwm_control и rc_input (RP2040/STM32).
- **failsafe_core.hpp / failsafe_core.cpp** — общая логика failsafe: `rc_vehicle::FailsafeInit(timeout_ms)`, `FailsafeUpdate(now_ms, rc_active, wifi_active)`, `FailsafeIsActive()`. Платформы передают время и таймаут, оборачивают в свой C-API.
- **slew_rate.hpp** — ограничение скорости изменения: `ApplySlewRate(target, current, max_change_per_sec, dt_ms)`. Используется в main loop RP2040 и STM32.

Наследники UartBridgeBase по платформам:
- **RP2040:** `Rp2040UartBridge` в `rp2040/main/uart_bridge.cpp` (Pico SDK: uart_*, gpio_*).
- **STM32:** `Stm32UartBridge` в `stm32/main/uart_bridge.cpp` (STM32Cube LL, пока заглушки).
- **ESP32:** `Esp32UartBridge` в `esp32/main/uart_bridge.cpp` (ESP-IDF: uart_driver_install, uart_write_bytes, uart_read_bytes).

Наследники SpiBase по платформам:
- **RP2040:** `SpiPico` в `rp2040/main/spi_pico.cpp` (Pico SDK: spi_*, gpio_*).
- **STM32:** `SpiStm32` в `stm32/main/spi_stm32.cpp` (STM32Cube LL: SPI2, PB12 NCS, PB13/14/15).

Везде сохраняется прежний C-API (`UartBridgeInit`, `ImuInit` / `ImuRead` / `ImuConvertToTelem` и т.д.), внутри создаётся экземпляр наследника и вызываются методы базового класса или `Mpu6050Spi`.

## Подключение

- **RP2040:** `main/CMakeLists.txt` — `RC_VEHICLE_COMMON_DIR`, include, `protocol.cpp`, `uart_bridge_base.cpp`, `mpu6050_spi.cpp`, `failsafe_core.cpp`.
- **STM32:** корневой `CMakeLists.txt` — `RC_VEHICLE_COMMON_DIR`, include, `protocol.cpp`, `uart_bridge_base.cpp`, `mpu6050_spi.cpp`, `failsafe_core.cpp`.
- **ESP32:** `main/CMakeLists.txt` — `INCLUDE_DIRS "../../common"`, `SRCS "../../common/protocol.cpp"`.
