# FW-R1 — Failsafe: нейтраль перезаписывается trim'ом

**Источник:** `firmware/CODE_REVIEW.md` → Review 2026-06-10, R1
**Приоритет:** HIGH (safety)
**Статус:** [x] Выполнено (2026-06-11)
**Файлы:** `common/control_loop_processor.cpp:33-34, 118-161`

## Проблема

В `ControlLoopProcessor::Step()` после `HandleFailsafe()` безусловно вызывается
`UpdatePwm()`. `HandleFailsafe()` при активном failsafe ставит `SetPwmNeutral()`
(`:134`), но в той же итерации `UpdatePwm()` выполняет
`SetPwm(0 + throttle_trim, 0 + steering_trim)` (`:157-159`).

Последствие: при ненулевом `throttle_trim` во время потери сигнала на моторы
подаётся trim вместо нейтрали — машина может медленно ползти при потерянной
связи. Это прямое нарушение контракта failsafe из ТЗ (`docs/ts.md`).

## Предлагаемое решение

`HandleFailsafe()` возвращает `bool` (failsafe активен). В `Step()`:

```cpp
const bool failsafe_active = HandleFailsafe();
if (!failsafe_active) {
  UpdatePwm(now, dt_ms);
}
UpdateTelemetry(now, dt_ms);  // телеметрия продолжает работать
```

`SetPwmNeutral()` остаётся единственной командой PWM при активном failsafe.
Заодно рассмотреть: не выполнять `UpdateStabilization()` при активном failsafe
(сейчас контроллеры обрабатывают нулевые команды впустую, а
`HandleFailsafe()` каждые 2 мс повторяет полный reset EKF/ПИД — reset
достаточно делать один раз на переходе в Active).

## Объём работ

- [ ] `HandleFailsafe()` → возвращает `bool`, `Step()` пропускает `UpdatePwm` при failsafe
- [ ] Reset подсистем (EKF, ПИД, kids, auto_drive) — однократно на переходе Inactive→Active (флаг `failsafe_was_active_`)
- [ ] Unit-тест в `tests/unit/test_control_loop_processor.cpp`: failsafe + `throttle_trim=0.1` → PWM остаётся нейтральным во всех последующих итерациях
- [ ] Unit-тест: восстановление сигнала → trim снова применяется

## Критерии приёмки

При активном failsafe `SetPwm()` не вызывается ни с какими аргументами;
последний PWM-вызов — `SetPwmNeutral()`. Существующие 14 тестов
`ControlLoopProcessor` проходят.
