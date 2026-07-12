# FW-RF10 — Заменить Result<T,E> на std::expected и output-параметры на возврат значений

**Приоритет:** LOW
**Статус:** [x] Частично смержено (этапы 1–5)
**PR:** [#185](https://github.com/DafeCpp/product_dev_course/pull/185)
**Зона:** `common/result.hpp`, `common/protocol.hpp`, `common/vehicle_control_platform.hpp`, `common/uart_bridge_base.hpp`

## Описание

Проект использует кастомный `Result<T,E>` на базе `std::variant` (`common/result.hpp`) с вспомогательными функциями `IsOk`, `GetValue`, `Err`, `Map` и т.д. Стандарт C++23 уже предоставляет `std::expected<T,E>`, который делает то же самое, но с нативной поддержкой монад (`and_then`, `transform`, `transform_error`), `has_value()`, `.value()`, `.error()`.

Цель: убрать кастомный `Result` и перейти на `std::expected` там, где это возможно.

## Почему

- `std::expected` — стандарт C++23, проект уже на C++23/C++26.
- Убирается boilerplate: `IsOk(r)` → `r.has_value()`, `GetValue(r)` → `*r`, `Err(E{...})` → `std::unexpected(E{...})`.
- Монадические цепочки (`and_then`, `transform`) работают нативно, без ручных `Map`/`AndThen`.
- Единый язык с остальной экосистемой (читаемость для внешних разработчиков).

## Объём

### Этап 1 (смержено) — Ядро: result.hpp, protocol, platform

- `common/result.hpp` — переопределён как тонкая обёртка над `std::expected` с backward-compatible хелперами (`IsOk`, `GetValue` и т.д.)
- `common/protocol.hpp` — `using Result = std::expected<T, ParseError>` вместо `rc_vehicle::Result`
- `common/vehicle_control_platform.hpp` — `Result<Unit, PlatformError>` → `std::expected<void, PlatformError>`
- `common/uart_bridge_base.hpp/.cpp` — обновлена сигнатура `ReceiveFrame` и все вызовы
- `common/protocol.cpp` — все `IsOk`/`GetValue`/`GetError` → `has_value()`/`*`/`.error()`, ошибки через `std::unexpected(...)`
- `common/vehicle_control_unified_init.cpp` — обновлены все вызовы платформы
- `common/stabilization_manager.cpp`, `common/calibration_manager.cpp` — обновлены вызовы `SaveStabilizationConfig`/`SaveCalib`
- `esp32_s3/main/vehicle_control_platform_esp32.hpp/.cpp` — `Ok<Unit,E>(Unit{})` → `Unit{}`, `Err<Unit,E>(e)` → `std::unexpected(e)`
- `tests/mocks/mock_platform.hpp` — `MockPlatform` и `FakePlatform` обновлены
- `tests/unit/test_protocol.cpp`, `tests/integration/test_uart_bridge.cpp`, `tests/integration/test_control_loop.cpp` — обновлены

### Этап 2 (будущее) — Sensor interfaces с output-параметрами

Датчики используют `int` + output-параметр. Кандидаты на `std::expected`:

| Интерфейс | Текущая сигнатура | Кандидат |
|-----------|-------------------|----------|
| `IImuSensor::Read(ImuData&)` | `int` + out param | `std::expected<ImuData, SensorError>` |
| `IMagSensor::Read(MagData&)` | `int` + out param | `std::expected<MagData, SensorError>` |
| `IImuSensor::Init()` | `int` | `std::expected<void, SensorError>` |
| `IMagSensor::Init()` | `int` | `std::expected<void, SensorError>` |
| `SpiBus::Init()` | `int` | `std::expected<void, SpiError>` |
| `SpiDevice::Init()` | `int` | `std::expected<void, SpiError>` |
| `SpiDevice::Transfer()` | `int` | `std::expected<void, SpiError>` |
| `VehicleControlPlatform::InitMag()` | `bool` | `std::expected<void, PlatformError>` |
| `VehicleControlPlatform::LoadComOffset()` | `bool` + out | `std::expected<std::array<float,2>, PlatformError>` |
| `VehicleControlPlatform::SaveMagCalib()` | `bool` | `std::expected<void, PlatformError>` |
| `VehicleControlPlatform::LoadMagCalib()` | `bool` + out | `std::expected<MagCalibData, PlatformError>` |

Требует: новые enum ошибок (`SensorError`, `SpiError`), обновление ESP32 HAL (`imu.cpp`, `mag.cpp`, `spi_esp32.cpp`), host-реализаций (`mpu6050_spi.cpp`, `lsm6ds3_spi.cpp`, `mmc5983_spi.cpp`), тестов (`test_mmc5983.cpp`).

### Калибровочные `Result`-структуры (оставить как есть)
- `SteeringTrimCalibration::Result`, `ComOffsetCalibration::Result`, `SpeedCalibration::Result` — это **не** `rc_vehicle::Result`, а простые POD-структуры с данными калибровки. Их **не трогаем** — они не связаны с `result.hpp`.

## Критерии приёмки

### Этап 1
- [x] `common/result.hpp` — тонкая обёртка над `std::expected`
- [x] Все `rc_vehicle::Result<T,E>` заменены на `std::expected<T,E>`
- [x] `common/protocol.hpp` компилируется и парсинг/билд работают
- [x] `common/vehicle_control_platform.hpp` компилируется
- [x] Host-тесты зелёные (813 tests passed)

### Этап 2
- [ ] Новые enum ошибок для датчиков
- [ ] `IImuSensor`, `IMagSensor`, `SpiBus`, `SpiDevice` перейти на `std::expected`
- [ ] `VehicleControlPlatform` output-параметры → возврат значений
- [ ] Все implementations обновлены
- [ ] Host-тесты зелёные

## Связанные

- **FW-RF9** — `[[nodiscard]]` для функций, возвращающих `Result` (предшественник; сейчас `[[nodiscard]]` распространится и на `std::expected`).
