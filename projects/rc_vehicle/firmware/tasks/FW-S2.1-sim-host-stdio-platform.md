# FW-S2.1 — C++ harness: `sim_host` exe + `StdioPlatform` + протокол

**Источник:** декомпозиция эпика [FW-S2](FW-S2-sil-physics-model-epic.md), 2026-06-17
**Тип:** инфраструктура тестирования (фундамент эпика)
**Язык:** C++
**Приоритет:** MEDIUM
**Статус:** [ ] Не начато
**Файлы (при реализации):** `tests/sim/stdio_platform.{hpp,cpp}`,
`tests/sim/sim_host_main.cpp`, `tests/CMakeLists.txt`,
`tests/unit/test_stdio_platform.cpp`.

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

- [ ] `sim_host` принимает кадр, гоняет реальный `ControlLoopProcessor`, отдаёт
  PWM + телеметрию.
- [ ] Работают оба режима: `batch` и `interactive` (+ `reset`).
- [ ] Время логическое: прогон N тиков не зависит от стенных часов, нет sleep.
- [ ] GTest на round-trip протокола (`test_stdio_platform.cpp`): кадр → Step → выход
  десериализуется обратно без потерь.
- [ ] Host-сборка GTest остаётся зелёной (814+ тестов).

## Связанные

- [FW-S2](FW-S2-sil-physics-model-epic.md) — эпик (архитектура).
- **Блокирует:** [FW-S2.2](FW-S2.2-python-replay-harness.md),
  [FW-S2.5](FW-S2.5-closed-loop-sil.md), [FW-S2.7](FW-S2.7-ci-fixtures.md).
- [FW-S1](FW-S1-telemetry-transport-format-research.md) — формат лога влияет на
  схему кадра протокола.
