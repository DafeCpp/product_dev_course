# FW-S2.1 — C++ harness: `sim_host` exe + `StdioPlatform` + протокол

**Источник:** декомпозиция эпика [FW-S2](FW-S2-sil-physics-model-epic.md), 2026-06-17
**Тип:** инфраструктура тестирования (фундамент эпика)
**Язык:** C++
**Приоритет:** MEDIUM
**Статус:** [x] Готово
**Файлы:** `tests/sim/stdio_platform.{hpp,cpp}`, `tests/sim/sim_host_main.cpp`,
`tests/CMakeLists.txt`, `tests/unit/test_stdio_platform.cpp`,
`common/vehicle_control_unified.{hpp,cpp}` (мини-рефактор: `BuildProcessor`/`HostStep`).

## Зачем

Фундамент всей SIL-симуляции (см. [архитектуру эпика](FW-S2-sil-physics-model-epic.md)):
реальный код прошивки должен исполняться на хосте под управлением внешнего драйвера
(Python). Для этого нужен исполняемый файл `sim_host`, который крутит **настоящий**
`ControlLoopProcessor`, а сенсоры/команды получает и выходы отдаёт через stdin/stdout.
Так в контуре оказывается весь штатный тракт прошивки (калибровка → Madgwick → EKF →
стабилизация → PWM), а не переписанная копия.

## Объём

**`StdioPlatform : VehicleControlPlatform`** — третья реализация платформы рядом с
`esp32` и `FakePlatform`:
- `ReadImu()` / `GetRc()` / `TryReceiveWifiCommand()` — читают текущий кадр из stdin.
- `SetPwm()` / `SetPwmNeutral()` / `PublishTelem()` — пишут выход в stdout.
- **Логическое время** (как в `FakePlatform`): `DelayUntilNextTick()` = no-op,
  `GetTimeMs/Us()` аккумулируют фиксированный `dt` (по умолчанию 2 мс = 500 Гц).
  Никаких реальных пауз — детерминированный прогон со скоростью CPU/IPC.
- Калибровка/конфиг загружаются командой `set-params` (включая режим identity-калибровки
  для replay «со средней точки»).

**`sim_host` (exe):**
- Сборка `ControlLoopContext` повторяет рецепт `ProcessorTest::SetUp`
  (`tests/unit/test_control_loop_processor.cpp`) — те же 10+ компонентов.
- Главный цикл: прочитать кадр → `processor.Step(now, dt)` → записать выход.
- Новый CMake-таргет в `tests/CMakeLists.txt`: линкует `COMMON_SOURCES` +
  `StdioPlatform` (без gtest).

**Протокол (старт — построчный текст, бинарь позже):**
- `reset` — сбросить состояние фильтров/EKF (между прогонами).
- `set-params <json/kv>` — калибровка, `StabilizationConfig`, dt, ориентация.
- `tick <сенсоры+RC>` → `<PWM+телеметрия>` — один шаг (interactive, для FW-S2.5).
- `batch` — принять весь трейс сенсоров, вернуть весь трейс выходов (для FW-S2.2).
- `flush` после каждого кадра (избежать дедлоков pipe).

## Критерии приёмки

- [x] `sim_host` принимает кадр, гоняет реальный `ControlLoopProcessor` (через
  `VehicleControlUnified::Init()` + `HostStep`), отдаёт PWM + телеметрию.
- [x] Работают оба режима: `batch` и `interactive` (`reset` = перезапуск процесса —
  отдельной in-band команды в MVP нет, не требуется).
- [x] Время логическое: прогон N тиков не зависит от стенных часов, нет sleep.
- [x] GTest (`test_stdio_platform.cpp`): парсинг входа/формат выхода + функциональные
  прогоны `HostStep` (нет NaN, throttle/steering ∈ [-1,1], failsafe → нейтраль).
- [x] Host-сборка GTest зелёная (820 тестов, +6 новых).

**Реализация (поправки к плану):** `StdioPlatform` — I/O-free (держит кадр + захват
выходов, как `FakePlatform`); stdin/stdout живёт только в `sim_host_main`. Причина:
IMU/RC/телеметрия сэмплируются с разной частотой → блокирующий I/O в `ReadImu`/`GetRc`
рассинхронил бы потоки. Полный тракт прошивки в контуре обеспечен реальным `Init()`
(не `ProcessorTest`-рецепт с null-хендлерами). Полнокадровая телеметрия —
`-DRC_TELEM_SEND_INTERVAL_MS=2` на таргете `sim_host`.

## Связанные

- [FW-S2](FW-S2-sil-physics-model-epic.md) — эпик (архитектура).
- **Блокирует:** [FW-S2.2](FW-S2.2-python-replay-harness.md),
  [FW-S2.5](FW-S2.5-closed-loop-sil.md), [FW-S2.7](FW-S2.7-ci-fixtures.md).
- [FW-S1](FW-S1-telemetry-transport-format-research.md) — формат лога влияет на
  схему кадра протокола.
