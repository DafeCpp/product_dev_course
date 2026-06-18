# FW-RF9 — Добавить [[nodiscard]] к функциям, возвращающим bool/Result

**Приоритет:** LOW
**Зона:** `common/*.hpp` — интерфейсы и менеджеры
**Статус:** [ ] открыта

## Описание

Ряд функций, возвращающих `bool` (индикатор успеха/состояния) или `Result<>`,
не имеет атрибута `[[nodiscard]]`. Это позволяет случайно проигнорировать
ошибку или невалидное состояние без предупреждения компилятора.

## Затронутые файлы

| Файл | Функция | Тип возврата |
|------|---------|--------------|
| `telemetry_log.hpp` | `GetFrame()` | `bool` |
| `telemetry_manager.hpp` | `GetLogFrame()` | `bool` |
| `telemetry_manager.hpp` | `GetEvent()` | `bool` |
| `telemetry_event_log.hpp` | `GetEvent()` | `bool` |
| `i_vehicle_control.hpp` | `GetLogFrame()` | `virtual bool` |
| `i_vehicle_control.hpp` | `GetEvent()` | `virtual bool` |
| `madgwick_filter.hpp` | `GetAdaptiveBetaEnabled()` | `bool` |
| `imu_calibration.hpp` | `IsValid()` | `bool` |
| `lpf_butterworth.hpp` | `IsConfigured()` | `bool` |
| `vehicle_control_unified.hpp` | `GetLogFrame()` | `bool override` |
| `vehicle_control_unified.hpp` | `GetEvent()` | `bool override` |

## Решение

Добавить `[[nodiscard]]` к каждой функции в таблице выше.

## Критерии приёмки

- [ ] Все функции в таблице имеют `[[nodiscard]]`
- [ ] `make type-check` проходит без ошибок
- [ ] Host-тесты зелёные
