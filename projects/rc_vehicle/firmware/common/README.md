# Общий код прошивок RC Vehicle

Папка `firmware/common/` содержит код, общий для прошивки ESP32-S3. Используется C++23 (или C++26 при поддержке тулчейна); стиль кода и правила по буферам описаны в [README корня firmware](../README.md).

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
- **rc_vehicle_common.hpp** — утилиты PWM/RC: `rc_vehicle::PulseWidthUsFromNormalized(...)`, `NormalizedFromPulseWidthUs(...)`, `ClampNormalized(value)`. Используются в pwm_control и rc_input (ESP32-S3).
- **failsafe_core.hpp / failsafe_core.cpp** — общая логика failsafe: `FailsafeUpdate(now_ms, rc_active, wifi_active)`, `FailsafeIsActive()`. Используется в control loop ESP32-S3.
- **slew_rate.hpp** — ограничение скорости изменения: `ApplySlewRate(target, current, max_change_per_sec, dt_ms)`. Используется в control loop ESP32-S3.

Платформа ESP32-S3 использует:
- **SPI (IMU):** `SpiBusEsp32` / `SpiDeviceEsp32` в `esp32_s3/main/spi_esp32.cpp` (ESP-IDF SPI master).
- UART-мост и протокол в `common/` используются при необходимости (например, для отладки или будущего расширения).

C-API (`ImuInit` / `ImuRead` и т.д.) сохраняется в платформенном коде; внутри вызываются методы базовых классов и `Mpu6050Spi`.

## Подключение

- **ESP32-S3:** `esp32_s3/main/CMakeLists.txt` — include `../../common`, исходники из `common/` (protocol, imu_calibration, madgwick_filter, control_components и др.).
