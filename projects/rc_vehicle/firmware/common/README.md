# Общий код прошивок RC Vehicle

Папка `firmware/common/` содержит код, общий для ESP32, RP2040 и STM32. Используется C++23 (или C++26 при поддержке тулчейна); стиль кода и правила по буферам описаны в [README корня firmware](../README.md).

## Содержимое

- **context.hpp** — контекст с регистрацией компонентов по типу (compile-time): `Context<UartBridgeBase, SpiDevice, ...>` — список типов задаётся при объявлении; `Set<T>(ref)` регистрирует по ссылке, `Get<T>()` возвращает компонент по типу T; опционально `get_time_ms`.
- **base_component.hpp** — базовый класс компонента: `BaseComponent` с виртуальными `Init()` (0/-1) и `Name()` для логов. Компоненты могут наследовать его и при необходимости принимать `Context&` в конструкторе.
- **protocol.hpp / protocol.cpp** — протокол UART (ESP32 ↔ MCU): namespace `rc_vehicle::protocol`, структуры `TelemetryData` и `CommandData` с методами, enum class `MessageType` и `ParseError`, классы `FrameBuilder`, `FrameParser` и `Protocol` для сериализации/десериализации кадров. Возврат `Result<T>` (std::variant) вместо 0 при ошибке. Старый API (deprecated) сохранён для обратной совместимости. Подробнее: [PROTOCOL_REFACTORING.md](PROTOCOL_REFACTORING.md).
- **uart_bridge_base.hpp** — абстрактный базовый класс UART-моста:
  - чисто виртуальные: `Init()`, `Write(data, len)`, `ReadAvailable(buf, max_len)`;
  - реализованы в базе (через протокол и буфер приёма): `SendTelem`, `ReceiveCommand` (для MCU), `SendCommand`, `ReceiveTelem` (для ESP32).
- **spi_base.hpp** — абстракции SPI:
  - `SpiBus`: виртуальный `Init()` для SPI-шины/периферии;
  - `SpiDevice`: виртуальные `Init()` и `Transfer(tx, rx)` (полнодуплексный обмен; `tx.size()==rx.size()`; реализация держит CS на время обмена);
  - алиас `SpiBase = SpiDevice` оставлен для обратной совместимости.
- **mpu6050_spi.hpp / mpu6050_spi.cpp** — драйвер MPU-6050 по SPI: структура `ImuData`, класс `Mpu6050Spi(SpiDevice*)` с `Init()`, `Read(ImuData&)`, `ConvertToTelem(const ImuData&, int16_t&, ...)`.
- **rc_vehicle_common.hpp** — утилиты PWM/RC: `rc_vehicle::PulseWidthUsFromNormalized(value, min_us, neutral_us, max_us)`, `NormalizedFromPulseWidthUs(pulse_us, ...)`, `ClampNormalized(value)`. Используются в pwm_control и rc_input (RP2040/STM32).
- **failsafe_core.hpp / failsafe_core.cpp** — общая логика failsafe: `rc_vehicle::FailsafeInit(timeout_ms)`, `FailsafeUpdate(now_ms, rc_active, wifi_active)`, `FailsafeIsActive()`. Платформы передают время и таймаут, оборачивают в свой C-API.
- **slew_rate.hpp** — ограничение скорости изменения: `ApplySlewRate(target, current, max_change_per_sec, dt_ms)`. Используется в main loop RP2040 и STM32.

Наследники UartBridgeBase по платформам:
- **RP2040:** `Rp2040UartBridge` в `rp2040/main/uart_bridge.cpp` (Pico SDK: uart_*, gpio_*).
- **STM32:** `Stm32UartBridge` в `stm32/main/uart_bridge.cpp` (STM32Cube LL, пока заглушки).
- **ESP32:** `Esp32UartBridge` в `esp32/main/uart_bridge.cpp` (ESP-IDF: uart_driver_install, uart_write_bytes, uart_read_bytes).

SPI по платформам (bus + device):
- **RP2040:** `SpiBusPico` / `SpiDevicePico` в `rp2040/main/spi_pico.cpp` (Pico SDK: spi_*, gpio_*).
- **STM32:** `SpiBusStm32` / `SpiDeviceStm32` в `stm32/main/spi_stm32.cpp` (STM32Cube LL).
- **ESP32 (ESP-IDF):** `SpiBusEsp32` / `SpiDeviceEsp32` в `esp32_s3/main/spi_esp32.cpp` (ESP-IDF SPI master).

Везде сохраняется прежний C-API (`UartBridgeInit`, `ImuInit` / `ImuRead` / `ImuConvertToTelem` и т.д.), внутри создаётся экземпляр наследника и вызываются методы базового класса или `Mpu6050Spi`.

## Подключение

- **RP2040:** `main/CMakeLists.txt` — `RC_VEHICLE_COMMON_DIR`, include, `protocol.cpp`, `uart_bridge_base.cpp`, `mpu6050_spi.cpp`, `failsafe_core.cpp`.
- **STM32:** корневой `CMakeLists.txt` — `RC_VEHICLE_COMMON_DIR`, include, `protocol.cpp`, `uart_bridge_base.cpp`, `mpu6050_spi.cpp`, `failsafe_core.cpp`.
- **ESP32:** `main/CMakeLists.txt` — `INCLUDE_DIRS "../../common"`, `SRCS "../../common/protocol.cpp"`.
