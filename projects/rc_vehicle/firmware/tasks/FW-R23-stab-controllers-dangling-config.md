# FW-R23 — Висячий указатель на конфиг у yaw/pitch/slip/oversteer

**Приоритет:** HIGH (UB в control loop на 500 Гц)
**Зона:** `common/stabilization_pipeline.cpp/.hpp`, `common/vehicle_control_unified_init.cpp`,
`common/control_loop_processor.cpp`
**Статус:** [ ] основной фикс готов (host-тесты зелёные), ждёт железа

## Контекст

Латентная часть FW-R21: тот же дефект, что у `KidsModeProcessor`, но в четырёх
контроллерах стабилизации. Вынесен отдельным PR (по плану FW-R21).

## Корневая причина

`vehicle_control_unified_init.cpp` создаёт **локальную** копию конфига и раздаёт
её адрес контроллерам:

```cpp
const StabilizationConfig cfg = stab_mgr_->GetConfig();  // локал
yaw_ctrl_.Init(cfg, ...);        // внутри: cfg_ = &cfg
pitch_ctrl_.Init(cfg, ...);      // cfg_ = &cfg
slip_ctrl_.Init(cfg, ...);       // cfg_ = &cfg
oversteer_guard_.Init(cfg, ...); // cfg_ = &cfg
```

После выхода из init-функции `cfg` уничтожается → все `cfg_` **висячие** (UB), и
это снимок на момент загрузки. Все четыре `Process()` **разыменовывают `cfg_`
каждый тик** (`stabilization_pipeline.cpp`):

- Yaw: `cfg_->yaw_rate.steer_to_yaw_rate_dps`, `cfg_->adaptive.*`
- Pitch: `cfg_->pitch_comp.{enabled,gain,max_correction}`
- Slip: `cfg_->slip_angle.target_deg`
- Oversteer: `cfg_->oversteer.{warn_enabled,slip_thresh_deg,rate_thresh_deg_s,throttle_reduction}`

### Почему «работало»

PID-коэффициенты идут отдельным путём — `StabilizationManager::SetConfig` зовёт
`yaw_ctrl_.SetGains`/`slip_ctrl_.SetGains` (`stabilization_manager.cpp:98-99`),
поэтому gains рантайм-актуальны. А прямые чтения `cfg_->…` в `Process()` —
нет: брались из висячего boot-снимка (UB, «повезло» с уцелевшим стеком).
Следствие: рантайм-правки `steer_to_yaw_rate_dps`, `adaptive`, `pitch_comp`,
порогов oversteer **не применялись** (смежно с FW-R15).

## Решение

Контроллеры больше не хранят `cfg_`. Конфиг приходит в `Process()` аргументом —
живой per-tick снимок `stab_cfg_` из control loop (как `UpdateWeights` и kids в
FW-R21). Тред-безопасно: снимок берётся под мьютексом раз в тик (раздача живой
ссылки на `config_` была бы гонкой — пишется с Core 0).

- `Process(const StabilizationConfig& cfg, …)` у всех четырёх; чтения `cfg.…`.
- `Init` больше не сохраняет `cfg_`; у yaw/slip остаётся параметр `cfg` только
  для начальных `SetGains`; у pitch/oversteer параметр `cfg` убран (не нужен).
- `cfg_` удалён из всех четырёх классов.
- Call-sites в `control_loop_processor.cpp` передают `stab_cfg_`.

## Критерии приёмки

- [x] Нет хранения `cfg_` в yaw/pitch/slip/oversteer; `Process()` получает конфиг
  per-tick.
- [x] Host-регрессия `PitchCompensatorTest.UsesConfigPassedPerCall_NotStored`:
  один процессор учитывает разные объекты конфига per-call.
- [x] Существующие тесты yaw/pitch/slip/oversteer зелёные (полный прогон 817).
- [ ] Железо: рантайм-правка `steer_to_yaw_rate_dps`/порогов oversteer
  применяется без перезагрузки.

## Связанные задачи

- **FW-R21** — kids-аналог (этот PR закрывает «латентную» часть R21).
- **FW-R22** — тоже трогает `YawRateController::Process` (стек: R23 поверх R22).
- **FW-R15** — рантайм-правки конфига не доезжали (та же семья).
