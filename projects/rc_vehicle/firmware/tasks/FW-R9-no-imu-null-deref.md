# FW-R9 — Загрузка без IMU падает на null-менеджерах (reboot-loop)

**Источник:** железная сессия 2026-06-12 (тест ветки test/wave-4-and-rf1)
**Приоритет:** CRITICAL (бесконечный reboot-loop при отказе датчика)
**Статус:** [x] Реализовано (PR)
**Файлы:** `common/vehicle_control_unified_init.cpp`, `common/vehicle_control_unified.hpp`

## Проблема

При отказе IMU `InitImuSubsystem()` выходит раньше, и `stab_mgr_`,
`calib_mgr_`, `telem_mgr_` остаются `nullptr`. Лог обещает graceful
degradation («IMU init failed — continuing without IMU»), но:

1. `InitializeComponents()` безусловно вызывает `stab_mgr_->GetConfig()`
   → `LoadProhibited` (EXCVADDR=0x14, мьютекс по смещению this=nullptr)
   ещё в `app_main` → паника → **бесконечный reboot-loop**
   (на железе накопилось crash_count=202).
2. Даже без п.1: ~15 методов VCU (`GetStabilizationConfig`, `GetLogInfo`,
   `ClearLog`, `StartCalibration`, ...) разыменовывают менеджеры без
   проверки — упал бы первый же WS-хендлер.

Backtrace с железа: `StabilizationManager::GetConfig()` ←
`VehicleControlUnified::InitializeComponents()`
(vehicle_control_unified_init.cpp:150) ← `Init()` ← `app_main`.

Путь существует с коммита caa77b0 (split VCU init) и не проявлялся,
пока IMU всегда инициализировался; вскрылся вместе с FW-R10.

## Решение

Семантика «нет IMU → нет стабилизации/калибровки/лога» сохранена,
менеджеры по-прежнему создаются только при живом IMU (контроллеры с
никогда не обновляющимся Madgwick опаснее, чем их отсутствие):

- `InitializeComponents()`: контроллеры инициализируются дефолтным
  `StabilizationConfig{}` при `stab_mgr_ == nullptr`;
- все обращения к менеджерам в `vehicle_control_unified.hpp` получили
  null-guard'ы (паттерн уже использовался в `SetKidsModeActive`):
  геттеры возвращают дефолты (`StabilizationConfig{}`, `"idle"`, 0, false),
  команды игнорируются / возвращают false;
- лог `InitTelemetryLog()` для этого пути — честный
  «TelemetryLog: disabled (no IMU)» вместо «failed to allocate (no PSRAM?)».

## Проверка

- [x] Прошивка собирается (esp32s3, ESP-IDF v6.0), host-тесты зелёные (802)
- [ ] Железо: с отключённым/неисправным IMU прошивка грузится, Wi-Fi/WS
      живут, команды отвечают дефолтами без перезагрузок
