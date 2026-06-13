# FW-R12 — Малый ход руля и газа в режимах Normal и Direct

**Источник:** железная сессия 2026-06-13 (тест ветки test/wave-4-and-rf1)
**Приоритет:** HIGH (управление почти не работает)
**Статус:** [ ] Не начато
**Файлы (кандидаты):** `common/control_loop_processor.cpp`,
`common/kids_mode_processor.cpp`, `common/drive_modes.cpp`,
`esp32_s3/main/pwm_control.cpp`

## Симптом

На железе колёса поворачиваются на руле **совсем немного** — и в режиме
**Normal**, и в режиме **Direct**. Газ, по ощущениям, тоже ограничен по ходу.
То есть полный диапазон команды (-1..+1) не доходит до сервы/ESC.

## Что уже проверено

PWM-выход маппит нормализованную команду в стандартный диапазон серво
корректно — ограничения там нет:

- `esp32_s3/main/config.hpp`: `PWM_MIN_US=1000`, `PWM_NEUTRAL_US=1500`,
  `PWM_MAX_US=2000`, `PWM_FREQUENCY_HZ=50`.
- `esp32_s3/main/pwm_control.cpp:102-112` — `PwmControlSetThrottle/Steering`
  через `PulseWidthUsFromNormalized(x, 1000, 1500, 2000)`. При `x=±1`
  получаем 1000/2000 мкс — полный ход.

Значит команда «придушена» **выше** PWM-слоя, до `SetPwm`.

## Кандидаты на причину

1. **Kids mode активен** — `common/kids_mode_processor.cpp:33,39`:
   `throttle = std::min(throttle, km.throttle_limit)`,
   `steering = std::clamp(steering, -km.steering_limit, km.steering_limit)`.
   Применяется в `control_loop_processor.cpp:105` **до** разветвления по
   drive-mode, т.е. бьёт и по Direct. Если после прошлых тестов остался
   включённым детский пресет (`kids_mode_active` + малые limit'ы в NVS),
   получим ровно этот симптом. **Проверить первым.**
2. **Конфиг стабилизации из NVS** с заниженными `steering_limit`/
   `throttle_limit` — применяется глобально.
3. **Серво/ESC физически имеют меньший рабочий диапазон**, чем 1000-2000
   мкс (тогда нужен конфиг endpoint'ов), — но это не объясняет, почему
   «немного»; скорее ПО.

## Как диагностировать на железе

- Вывести в DIAG/серийник `commanded_steering`/`commanded_throttle` **и**
  итоговый pulse_us (или нормализованное значение перед `SetPwm`). Сравнить:
  команда ±1 → выход ±1? Если команда уже мала — смотреть kids/limits;
  если команда ±1, а ход мал — смотреть серво/механику.
- Через Web UI: `Get Stab Config` → проверить `kids_mode_active`,
  `throttle_limit`, `steering_limit`; `Toggle Kids Mode (active:false)` и
  повторить.

## Критерии приёмки

- [ ] В Direct полный ход руля/газа соответствует команде ±1 (серва ходит
      от упора до упора, ESC даёт полный диапазон)
- [ ] Найдена и устранена причина «придушивания» (или задокументировано,
      что это был активный kids-режим — тогда фикс в UX/сбросе)
