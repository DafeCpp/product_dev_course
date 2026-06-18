# FW-S2.8 — SIL: plausibility-сценарии + детский режим (drive-mode в sim_host)

**Источник:** запрос 2026-06-18 (валидация работоспособности модели), follow-up к
[FW-S2.5](FW-S2.5-closed-loop-sil.md)
**Тип:** инфраструктура тестирования (closed-loop сценарии)
**Язык:** C++ (расширение `sim_host`) + Python (тесты)
**Приоритет:** MEDIUM
**Статус:** [x] Готово
**Файлы:** `tests/sim/stdio_platform.{hpp,cpp}`, `tests/sim/sim_host_main.cpp`,
`sim/simlib/closed_loop.py`, `sim/tests/test_plausibility.py`,
`sim/tests/test_kids_mode.py`.

## Зачем

Простые «здравые» проверки работоспособности модели в контуре (газ → разгон →
накат до остановки), и — главное — closed-loop регресс на **детский лимит скорости
(FW-R21)**: полный газ в Kids должен резаться (throttle_limit + лимитер скорости),
в отличие от Normal. Для этого `sim_host` нужно уметь включать режим вождения.

## Объём

- **`sim_host`:** флаги `--drive-mode <normal|kids|sport|drift|directlaw>` и
  `--speed-limit <м/с>`. `StdioPlatform` отдаёт `StabilizationConfig` с заданным
  режимом и (для лимита) `kids_mode.speed_limit_enabled + max_speed_ms`. В выход
  добавлены `kids_mode_active`, `kids_throttle_limit` (наблюдаемость).
- **`ClosedLoopSim`:** параметры `drive_mode` / `speed_limit`.
- **Plausibility** (`test_plausibility.py`): газ 2 с → отпустил → накат до ~0; нет
  команды → стоит; задний ход.
- **Детский режим** (`test_kids_mode.py`): полный газ в Kids ограничивает скорость
  в разы против Normal; чем ниже `--speed-limit`, тем ниже установившаяся скорость.

## Критерии приёмки

- [x] `sim_host --drive-mode kids --speed-limit X` включает детский режим в реальном
  тракте прошивки; `kids_mode_active=1` в выходе.
- [x] Kids при полном газе ограничивает скорость (< 0.5× от Normal), газ ≤ throttle_limit.
- [x] Plausibility: разгон→накат→остановка; pytest зелёный (28 по `firmware/sim`).

## Наблюдения

- **R21 подтверждён исправленным:** на старом base (до фикса FW-R21) SIL показывал, что
  Kids-лимиты не применяются (висячий указатель `cfg_` в `KidsModeProcessor`, конфиг
  читался из стек-локала `InitializeComponents`). После фикса (Process получает живой
  снимок конфига) лимит срабатывает — этот тест теперь страхует регресс.
- **EKF-скорость занижает** истинную скорость модели (~1.5 vs 2.5 м/с) — лимитер
  гейтит по EKF, поэтому порог срабатывает по заниженной оценке. Перекликается с
  находкой [FW-S2.6](FW-S2.6-model-validation.md); кандидат на калибровку EKF-скорости.

## Связанные

- [FW-S2.5](FW-S2.5-closed-loop-sil.md) — closed-loop гарнесс (база).
- **FW-R21** — детский лимит скорости (регресс-кейс), **FW-R22** — задний ход.
- [FW-S2.6](FW-S2.6-model-validation.md) — фиделити EKF-скорости.
