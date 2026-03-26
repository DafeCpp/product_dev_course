# Firmware Refactoring — Status

560 тестов (544 unit + 16 integration), 0 глобальных классов, `main.cpp` 139 строк.
`VehicleControlUnified.cpp` — **89 строк** ✅ (цель <200 достигнута).

---

## Завершённые задачи

| Задача | Описание |
|--------|----------|
| Phase 6.3 | 16 интеграционных тестов через `SimPlatform`/`FakePlatform` |
| Phase 6.4 | WS Handler DI — 22 хендлера получают `IVehicleControl&` через параметр |
| AutoDriveCoordinator | 4 авто-процедуры с централизованным взаимным исключением |
| TelemetryBuilder | `BuildTelemetrySnapshot` / `BuildLogFrame` → free functions |
| DiagnosticsReporter | `PrintDiagnostics` → free function |
| VehicleEkf::UpdateFromImu | Predict + GyroZ + ZUPT за один вызов |
| ControlLoopHelpers | `BuildSensorSnapshot`, `CorrectImuForComOffset`, `BuildAutoDriveInput`, `SelectControlSource`, `UpdatePwmWithSlewRate`, `HandleAutoDriveCompletion`, `BuildSelfTestInput` → free functions |
| InitImuSubsystem / InitTelemetryLog | Блоки инициализации извлечены из `Init()` |
| ControlLoopProcessor | Тело `ControlTaskLoop` (~220 строк) → отдельный класс; `ControlTaskLoop` стал 22 строки |
| vehicle_control_unified_init.cpp | `Init`, `InitImuSubsystem`, `InitTelemetryLog`, `InitializeComponents` → отдельный файл; VCU main .cpp = 89 строк |
| CoM Offset Calibration | Круговая калибровка CW+CCW с вычислением смещения IMU |
| CoM Accel Correction | Коррекция акселерометра: центростремительная + тангенциальная |
| TestRunner | Фреймворк авто-тестов (Straight/Circle) |
| Steering Trim Calibration | Авто-калибровка trim + fix `IsActive()` |
| Web UI Charts | Canvas-графики телеметрии в реальном времени |
| Binary Telemetry HTTP | Бинарный эндпоинт `/log.bin` |

---

## Итог рефакторинга VCU

| Файл | Строк |
|------|-------|
| `vehicle_control_unified.cpp` | **89** (оркестрация: loop + делегаторы) |
| `vehicle_control_unified_init.cpp` | 142 (инициализация) |
| `vehicle_control_unified.hpp` | 334 |
| `control_loop_processor.cpp` | 176 |
| `control_loop_processor.hpp` | 97 |
| `control_loop_helpers.hpp/cpp` | ~180 |
| Было: `vehicle_control_unified.cpp` | 868 |

---

## Открытые пункты

| Пункт | Приоритет | Описание |
|-------|-----------|----------|
| `InitializeComponents` split | Низкий | Незначительный выигрыш (~10 строк) |
| `test_ws_command_registry.cpp` | Низкий | Требует mock `esp_http_server.h` |
