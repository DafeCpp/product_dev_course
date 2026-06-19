# FW-R21 — Kids Mode: ограничения скорости/газа не применяются (висячий указатель на конфиг)

**Приоритет:** HIGH (safety — детский режим)
**Зона:** `common/vehicle_control_unified_init.cpp`, `common/kids_mode_processor.cpp`,
`common/control_loop_processor.cpp` (+ латентно: `common/stabilization_pipeline.cpp`)
**Статус:** [ ] основной фикс kids — готов (host-тесты зелёные), ждёт железа;
латентный фикс 4 контроллеров — отдельный PR

## Симптом (нашёл пользователь)

В детском режиме (Kids) ограничения скорости не работают: машина разгоняется,
как будто лимиты не применяются. По наблюдению — не срабатывает ни
**EKF speed limit** (`max_speed_ms`, секция 5 `Process()`), ни, под вопросом,
**статический throttle cap** (`throttle_limit`, секция 1). Симптом «оба сразу»
указывает не на конкретный лимитер, а на то, что `KidsModeProcessor::Process()`
целиком уходит в ранний `return`.

## Подтверждение на железе (`telemetry_log_kids_test_17_06.csv`, 30.9 с)

- `rc_throttle == cmd_throttle` в **100%** кадров (2842/2842) — команда газа
  байт-в-байт равна сырому RC-входу, т.е. `KidsModeProcessor::Process()` не
  тронул команду (ранний выход на `IsActive()`).
- `cmd_throttle` превышал кап Child 0.30 в **127** кадрах (до +0.464) —
  `throttle_limit` не применялся.
- `speed_ms` превышал 1.0 м/с в **148** кадрах (макс 1.55) — speed limit неэффективен.
- При этом `cmd_steering != rc_steering` в ~40% кадров → рулевая стабилизация
  (yaw-rate) работала на устаревшем boot-снимке. Та самая асимметрия:
  стабилизация жива, kids-лимиты мертвы.

## Корневая причина

### 1. Висячий указатель на локальный конфиг (основная)

`vehicle_control_unified_init.cpp:155-161`:

```cpp
const StabilizationConfig cfg =
    stab_mgr_ ? stab_mgr_->GetConfig() : StabilizationConfig{};   // ЛОКАЛЬНАЯ копия
yaw_ctrl_.Init(cfg, ekf_, imu_handler_.get());
pitch_ctrl_.Init(cfg, madgwick_, imu_handler_.get());
slip_ctrl_.Init(cfg, ekf_, imu_handler_.get());
oversteer_guard_.Init(cfg, ekf_, imu_handler_.get());
kids_processor_.Init(cfg, ekf_, imu_handler_.get());
```

`GetConfig()` возвращает **по значению** (`stabilization_manager.hpp:45`), то есть
`cfg` — локальная переменная функции инициализации. `KidsModeProcessor::Init`
(`kids_mode_processor.cpp:12`) сохраняет `cfg_ = &cfg` — указатель на этот локал.
Когда init-функция возвращает управление, `cfg` уничтожается → `cfg_` **висячий**
(UB), и при этом указывает на **снимок конфига на момент загрузки** (режим = тот,
что лежал в NVS при старте, обычно НЕ Kids).

`Process()` в самом начале (`kids_mode_processor.cpp:21-23`):

```cpp
if (!cfg_ || !IsActive()) return;   // IsActive() == cfg_->mode == DriveMode::Kids
```

читает `cfg_->mode` из висячего/устаревшего снимка → `IsActive()` обычно `false`
→ **ранний выход** → не применяется НИЧЕГО: ни `throttle_limit`, ни slew, ни
anti-spin, ни accel limit, ни speed limit. Это и есть «оба сразу не работают».

Даже если на старте режим случайно был Kids, значения лимитов
(`throttle_limit`, `max_speed_ms`, пресеты) заморожены на момент инициализации и
**не отслеживают** смену режима/пресета по WebSocket в рантайме.

### 2. Почему стабилизация при этом работает, а Kids — нет

Тот же `cfg_ = &cfg` есть и у yaw/pitch/slip/oversteer
(`stabilization_pipeline.cpp:25,76,105,139`) — это латентный UB. Но их рабочее
поведение каждый тик переинициализируется живым конфигом:
`control_loop_processor.cpp:128` → `stab_mgr_->UpdateWeights(stab_cfg_, dt_ms)`,
куда передаётся живой per-tick снимок `stab_cfg_` (`control_loop_processor.cpp:39`).
А `KidsModeProcessor::Process()` **не принимает конфиг аргументом** и читает
`cfg_` напрямую — поэтому полностью зависит от висячего/устаревшего указателя.

### 3. EKF speed limit ненадёжен и после фикса п.1 (вторичная)

Секция 5 режет газ по `ekf_->GetSpeedMs() = sqrt(vx²+vy²)`. EKF — IMU-only, без
датчика колёс; по собственной документации (`vehicle_ekf.hpp:68-70`) «vx и vy
накапливают дрейф из-за интеграции ускорений». Абсолютная скорость
недостоверна → порог `max_speed_ms` триггерит ненадёжно даже при исправном
указателе. Надёжный лимит в Kids — статический `throttle_limit` (без сенсоров).

## Почему host-тесты зелёные, а железо — нет

В `test_kids_mode.cpp` `cfg` — локал теста, который **переживает** процессор и в
котором `mode` сразу выставлен в Kids. Поэтому ни висячий указатель, ни
устаревший снимок не проявляются. Баг живёт только в реальном init-пути
(локал умирает) + при смене режима в рантайме.

## Решение

### Основное — отдать живой конфиг в Process() (рекоменд.)

Согласовано с FW-RF5 (один снимок конфига на итерацию) и с тем, как
`UpdateWeights` уже получает `cfg` параметром:

- `KidsModeProcessor::Process(float& thr, float& steer, uint32_t dt_ms,
  float fwd_accel, const StabilizationConfig& cfg)` — лимиты и `IsActive()`
  читать из переданного `cfg`, а не из `cfg_`.
- В `control_loop_processor.cpp:139` передать живой `stab_cfg_`.
- Убрать хранение `cfg_` в `KidsModeProcessor` (оставить только `ekf_`/`imu_`)
  — снять висячий указатель в корне.

### Латентное — тот же висячий указатель у контроллеров (ПОДТВЕРЖДЁН активным)

yaw/pitch/slip/oversteer тоже хранят `&cfg` на тот же мёртвый локал и
**разыменовывают `cfg_` в своём `Process()` каждый тик** (PID-коэффициенты,
adaptive, пороги — `stabilization_pipeline.cpp:46,52-55,83,91-93,120,147,173-178`).
Это активный UB (чтение освобождённого стека на 500 Гц) + конфиг заморожен на
boot (рантайм-ретюнинг PID не применяется — связано с FW-R15). `UpdateWeights`
считает только fade-веса, эти значения НЕ маскирует — «работает» лишь потому,
что boot-конфиг случайно уцелел на стеке. Жить с раздачей живой ссылки нельзя:
`config_` под мьютексом и пишется с другого ядра (Core 0 WS-хендлер) → гонка.
Корректное решение — отдать per-tick снимок `stab_cfg_` в `Process()` каждого
(как уже делает `UpdateWeights`). Отдельный PR (4 контроллера + их тесты).

### Вторичное — надёжность speed limit

Решить отдельно: `throttle_limit` оставить основным лимитом Kids; EKF
`max_speed_ms` — либо явно пометить как best-effort (флаг
`speed_limit_active` уже есть в телеметрии), либо вынести в спайк по добавлению
датчика скорости/ZUPT-доверия. В рамках R21 — починить, чтобы пайплайн Kids
вообще исполнялся; достоверность EKF-скорости — не блокер.

## Критерии приёмки

- [x] Host-тест-регрессия: `Init` без Kids, затем перевод в Kids в рантайме →
  `throttle_limit` применяется (`KidsModeProcessorRuntimeSwitchTest.
  LimitsApplyAfterRuntimeSwitchToKids`; до фикса упал бы).
- [x] Host-тест: в Kids `throttle=1.0` режется до `throttle_limit`
  (`ThrottleLimitAppliedToForwardThrottle`).
- [x] Host-тест: speed limit срабатывает при `ekf.GetSpeedMs() > max_speed_ms`
  (`KidsModeSpeedLimitTest.*`).
- [x] Нет хранения указателя на конфиг в `KidsModeProcessor` — `Process()`
  получает живой снимок `stab_cfg_` per-tick. Полный прогон: 814 тестов зелёные.
- [ ] Железо: выбрать Kids, проверить, что машина держит `throttle_limit`;
  телеметрия `kids_mode.throttle_limit` соответствует активному пресету.

## Связанные задачи

- **FW-R15** — кастом-настройка теряется при смене режима: та же семья
  «конфиг не отслеживает рантайм-состояние».
- **FW-RF5** — один снимок конфига на итерацию: паттерн, который надо
  распространить на kids_processor (передавать снимок в Process()).
- **FW-RF4** — единый источник истины kids-пресетов.
- **FW-R19** — снимок конфига в телеметрии.
