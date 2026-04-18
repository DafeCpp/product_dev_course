# Задачи на реализацию для программы испытаний

Список фич и изменений в прошивке, необходимых для выполнения [TESTING_PROGRAM.md](TESTING_PROGRAM.md).

---

## 1. Автоматическая калибровка trim руля ✅

**Цель:** автоматически подобрать `steering_trim` так, чтобы машинка ехала прямо при `steering = 0`.

**Алгоритм:**
1. Режим DirectLaw, фиксированный газ ~30%
2. Сбор данных: средний `yaw_rate` за 3-5 сек
3. Итеративная коррекция: `trim += -K * mean_yaw_rate` (несколько проходов или один проезд с online-подбором)
4. Критерий сходимости: `|mean_yaw_rate| < 1 dps`
5. Сохранение в NVS через `StabilizationConfig`

**Реализация:**
- [x] Класс `SteeringTrimCalibration` — `common/steering_trim_calibration.hpp`
- [x] WS-команда `calibrate_trim` — `esp32_s3/main/ws_command_handlers.cpp`
- [x] Интеграция в `AutoDriveCoordinator` — `common/auto_drive_coordinator.hpp:62-70`
- [x] Обратная связь через телеметрию

**Throttle trim:** ручная настройка через Web UI (уже работает). Автоматизация сложнее — нужен внешний датчик скорости или визуальный контроль "ползёт/не ползёт".

---

## 2. Круговая калибровка IMU→CoM ✅

**Цель:** определить смещение (rx, ry) IMU относительно центра масс.

**Реализация:**
- [x] Класс `ComOffsetCalibration` — `common/com_offset_calibration.hpp`
- [x] Два прохода CW и CCW с детекцией steady-state
- [x] Расчёт (rx, ry) из системы уравнений
- [x] Поля `com_offset[2]` в `ImuCalibData` — `common/imu_calibration.hpp:18-20`
- [x] WS-команда `start_com_calib` — `esp32_s3/main/ws_command_handlers.cpp`
- [ ] Unit-тест расчёта offset из синтетических данных кругового движения

---

## 3. Коррекция акселерометра по смещению IMU от CoM ✅

**Цель:** в каждом цикле (500 Hz) вычитать из показаний акселерометра вклад от вращения вокруг CoM.

**Формула:**
```
a_corrected = a_imu - α × r - ω × (ω × r)
```
где `r = (rx, ry, 0)`, `ω = (0, 0, gyro_z)`, `α = (0, 0, d(gyro_z)/dt)`

**Реализация:**
- [x] Метод `CorrectForComOffset()` — `common/imu_calibration.hpp:94-108`, `common/imu_calibration.cpp:177`
- [x] Вызов после `calib_.Apply()` до передачи в EKF/Madgwick
- [x] Численное дифференцирование `α` с LPF

---

## 4. Расширение телеметрии для испытаний ✅

**Цель:** добавить данные, необходимые для анализа результатов тестов.

- [x] `cmd_throttle`, `cmd_steering` — команды до trim/slew — `telemetry_log.hpp:32-33`
- [x] `ekf_vx_var`, `ekf_vy_var`, `ekf_r_var` — ковариации EKF — `telemetry_log.hpp:34-36`
- [x] `test_marker` (uint8) — маркер теста — `telemetry_log.hpp:37`
- [x] `HandleGetLogData` и WebSocket streaming обновлены

> Итоговый размер `TelemetryLogFrame` = 104 байта × 60k = 6.2 МБ PSRAM (из 16 МБ — допустимо).

> **Примечание:** поля `trim_throttle` / `trim_steering` не добавлены — при необходимости дополнить.

---

## 5. Бинарный экспорт телеметрии

**Цель:** быстрое скачивание лога для офлайн-анализа (текущий JSON-экспорт медленный для 60k фреймов).

- [ ] HTTP GET эндпоинт `/api/log/download` — отдаёт raw binary (little-endian, packed structs)
- [ ] Заголовок: magic, version, frame_count, frame_size, field descriptors
- [ ] Python-скрипт для парсинга в pandas DataFrame
- [ ] Альтернатива: CSV-export через HTTP (проще парсить, но больше по объёму)

---

## 6. Тестовые автоматические режимы ✅

**Цель:** автоматизировать типовые манёвры из программы испытаний.

- [x] **Прямолинейный проезд** — `TestType::Straight` — `common/test_runner.hpp`
- [x] **Круговой проезд** — `TestType::Circle` — `common/test_runner.hpp`
- [x] **Step response** — `TestType::Step` — `common/test_runner.hpp`
- [x] `TestRunner` с state machine: `Idle → Accelerate → Cruise/StepExec → Brake → Done/Failed`
- [x] WS-команда `start_test` — `esp32_s3/main/ws_command_handlers.cpp:523`
- [x] Safety: RC override прерывает тест, failsafe → немедленная остановка
- [x] Автоматическая расстановка `test_marker` в телеметрии

---

## 7. Web UI для испытаний (частично)

**Цель:** удобный интерфейс для запуска тестов и просмотра результатов.

- [x] WS-команды `start_test`, `calibrate_trim`, `start_com_calib` зарегистрированы
- [x] Streaming телеметрии реализован
- [ ] Отдельная страница/вкладка "Testing" в Web UI
- [ ] Кнопки запуска тестовых режимов (straight, circle, step) с параметрами в UI
- [ ] Live-графики: yaw_rate, speed, accel, slip_angle во время теста
- [ ] Скачивание лога (бинарный/CSV) — зависит от Задачи 5
- [ ] Отображение текущих trim-значений и CoM offset

---

## 8. Интеграционные тесты (Control Loop Simulator) (частично)

Связано с Phase 6.3 из [REFACTORING_PROPOSAL.md](REFACTORING_PROPOSAL.md).

- [x] `FakePlatform` / `SimPlatform` — `tests/mocks/mock_platform.hpp:134`
- [x] Сценарий: failsafe → проверка остановки за 250ms — `tests/integration/test_control_loop.cpp`
- [x] Базовые инварианты control loop
- [ ] Физическая модель динамики (bicycle model) в `FakePlatform`
- [ ] Сценарий: прямолинейный проезд → проверка EKF drift
- [ ] Сценарий: круговой проезд → проверка CoM-коррекции акселерометра
- [ ] Сценарий: step response → проверка PID settling time
- [ ] Сценарий: trim calibration → проверка сходимости

---

## Приоритеты

| Приоритет | Задача | Статус | Блокирует |
|-----------|--------|--------|-----------|
| **P0** | 1. Авто-калибровка trim руля | ✅ Готово | — |
| **P0** | 4. Расширение телеметрии | ✅ Готово | — |
| **P1** | 6. Тестовые режимы (straight, circle, step) | ✅ Готово | — |
| **P1** | 2. Круговая калибровка CoM | ✅ Готово | — |
| **P1** | 3. Коррекция акселерометра | ✅ Готово | — |
| **P2** | 5. Бинарный экспорт телеметрии | ❌ Не начато | Офлайн-анализ, Задача 7 |
| **P2** | 7. Web UI для испытаний | 🔶 Частично | Удобство работы |
| **P3** | 8. Интеграционные тесты | 🔶 Частично | Регрессия после изменений |

---

## Зависимости

```
1. Trim калибровка ──────────────────────────────────────┐
                                                          ├→ 7. Web UI (частично готов)
2. Круговая калибровка CoM ──→ 3. Коррекция акселерометра ┤
                                                          │
4. Расширение телеметрии ──→ 5. Бинарный экспорт ─────────┤
                                                          │
6. Тестовые режимы ───────────────────────────────────────┘
                           ↓
                    8. Интеграционные тесты (частично)
```
