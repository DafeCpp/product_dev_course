# FW-R22 — Рулевая стабилизация дёргает руль в реверсе (неверный знак ОС)

**Приоритет:** HIGH (управляемость/безопасность)
**Зона:** `common/stabilization_pipeline.cpp` (`YawRateController::Process`),
`common/config.hpp`/`stabilization_config`
**Статус:** [ ] открыта

## Симптом (нашёл пользователь)

При движении **назад** колёса сами дёргаются влево-вправо, хотя руль не трогаешь.

## Подтверждение на железе (оба лога 17_06)

Выборка REVERSE (`rc_throttle < -0.05`), руль водителя почти не трогается:

| лог | n | rc_steer std | applied steer std | \|steer\|max | смен знака | corr(injected, yaw_rate) |
|-----|---|--------------|-------------------|--------------|------------|--------------------------|
| kids_test | 924 | 0.042 | 0.175 | 0.556 | 60 | −0.54 |
| normal | 1108 | 0.034 | 0.139 | 0.444 | 199 | −0.54 |

- «Впрыснутый» руль `injected = cmd_steering − rc_steering` сильно
  антикоррелирует с `yaw_rate_dps`/`gz` (−0.54 реверс, до −0.79 форвард) →
  источник = yaw-rate стабилизатор, контрулящий по гироскопу, не RC.
- normal: **276** кадров реверса с `rc_steering≈0`, но `|steering|>0.1`; все на
  реальной скорости 0.22–0.79 м/с (не стоянка — гейт FW-R17 проходит штатно).
- Воспроизводится **в обоих режимах** (normal и kids) → дефект самого
  yaw-rate-стабилизатора, не связан с FW-R21.

## Корневая причина

`YawRateController::Process` (`stabilization_pipeline.cpp:31-60`):

```cpp
const float omega_desired = cfg_->yaw_rate.steer_to_yaw_rate_dps * steering;
const float omega_actual  = imu_->GetFilteredGyroZ();
const float pid_out       = pid_.Step(omega_desired - omega_actual, dt_sec);
steering = std::clamp(steering + pid_out * stab_w * mode_w * adaptive_scale, -1, 1);
```

Это yaw-rate feedback: подмешивает руль, чтобы измеренный рыскание совпало с
желаемым. Знак зашит под движение **вперёд** (положительный руль → положительный
yaw). Гейт один — по модулю скорости (`GetSpeedMs() < kMinStabSpeedMs`, FW-R17),
направление не учитывается.

В реверсе связь руль→рыскание **инвертируется** (как при сдаче назад на машине).
При `steering≈0` → `omega_desired≈0`; машина в реверсе вращается → `omega_actual≠0`
→ PID добавляет руль, чтобы погасить вращение, но в реверсе этот руль усиливает
вращение (обратная связь становится **положительной**) → автоколебания
лево-право (hunting). `GetSpeedMs()` ≥ 0 — знак `vx` потерян, гейт реверс не ловит.

## Решение

Гейтить yaw-rate-стабилизацию по **направлению движения**, а не только по модулю
скорости. В реверсе — пропускать руль как есть и держать PID в сбросе (как уже
сделано для стоянки в FW-R17):

```cpp
if (ekf_->GetSpeedMs() < kMinStabSpeedMs || IsReversing()) {
  pid_.Reset();
  return;
}
```

**Источник направления — знак команды газа, а не EKF `vx`.** EKF — IMU-only,
`vx` ненадёжен: в `telemetry_log_normal` `ekf_vx_var` доходит до 193 (std ~14 м/с
при реальной <1 м/с) и >10 в 54% кадров. Знак `cmd_throttle`/`rc_throttle`
(или направление MotionDriver) — надёжный индикатор реверса. Передать признак
реверса в `Process()` так же, как конфиг per-tick.

Альтернатива (сложнее, выгода спорна на RC-масштабе): инвертировать знак
коррекции при реверсе и оставить стабилизацию активной. Рекомендация —
**отключать в реверсе** (просто, безопасно).

## Критерии приёмки

- [x] Host-тест: при `reversing=true` `YawRateController::Process` не меняет
  `steering`, PID в reset (`Reversing_NoCorrection_EvenWithError`,
  `Reversing_ResetsPid`).
- [x] Host-тест: форвард-поведение не изменилось
  (`Forward_StillCorrects_WhenNotReversing` + существующие yaw-тесты; 816 зелёных).
- [ ] Железо/реплей: в реверсе `cmd_steering == rc_steering` (нет впрыска),
  дёрганье руля исчезает; форвард-стабилизация работает как раньше.

## Реализация

`YawRateController::Process` получил параметр `bool reversing` (default false);
при `reversing` — `pid_.Reset(); return;` (рядом с гейтом FW-R17 по скорости).
Источник в control loop — `commanded_throttle_ < 0.0f`
(`control_loop_processor.cpp`), т.к. EKF `vx` ненадёжен.

## Связанные задачи

- **FW-R17** — гейт стабилизации по скорости (модуль): здесь добавляем гейт по
  направлению.
- **FW-R21** — kids-лимиты (другой дефект того же лога).
- EKF `vx`-дрейф (`ekf_vx_var` до 193) — общий с ненадёжностью kids speed limit
  (FW-R21, секция «надёжность speed limit»); кандидат на отдельный спайк/датчик
  скорости.
