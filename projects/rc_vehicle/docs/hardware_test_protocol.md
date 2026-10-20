# Протокол испытаний на железе (LOS-364)

Единый чеклист сессии на машине. Порядок — от безопасного к динамике: следующий
этап начинается, только когда предыдущий прошёл. Для каждого пункта указаны
критерий PASS, что писать в лог, чем разбирать и какой SIL-сценарий служит
эталоном (тот же критерий на модели; `firmware/sim/tests/`, запуск — ниже).

Подробные шаги по отдельным темам остаются в исходных документах:
[`firmware/TESTING_PROGRAM.md`](../firmware/TESTING_PROGRAM.md) (фазы 0–5),
[`indoor_test_checklist.md`](indoor_test_checklist.md) (D1–D5, поведение ESC),
[`stabilization/roadmap_ru.md`](stabilization/roadmap_ru.md). Номера `IC D1#n`
ниже ссылаются на чеклист, `TP Фn` — на программу испытаний.

Пути — от `projects/rc_vehicle/`.

## Мета сессии

- **Дата / место / покрытие:** _______________
- **Прошивка:** коммит `develop` _______________ (`idf.py` build, NVS-версия конфига ___)
- **Железо:** IMU (LSM6DS3 / MPU-6500) ___, ESC ___, батарея ___ S, напряжение до/после ___ / ___ V
- **Пульт RC:** да / нет (yaw-step и `rc_steering` в логе — только с пульта)

## Подготовка

1. Прошить `develop` (из `projects/rc_vehicle/`): `make build && make flash`; serial — `make monitor`.
2. Ноутбук в AP машины, web UI открыт, WebSocket `Подключено`.
3. **Лог.** Основной — кольцевой буфер PSRAM 100 Гц (кадр 132 байта, события
   в тех же строках). Перед каждой записью «Очистить», после — кнопка CSV
   (выгружается весь буфер, несмотря на подпись «CSV (500)»). Буфер —
   52000 кадров ≈ **8.7 мин**: дольше старые кадры затираются. Имена файлов:
   `<дата>_<пункт>_<вариант>.csv`, например `2026-10-25_7.1_normal.csv`.
4. **UDP-запись (`tools/udp_telem.py`) пока не использовать для разбора**:
   скрипт ждёт старый кадр на 80 байт, в CSV не попадают `drive_mode`, `rc_ok`,
   `wifi_ok`, `failsafe_active`, `kids_*`, `cmd_*`, `test_marker`. На нём верны
   только `gyro-bias`, `ekf-dist` и `yaw-step` в Normal.
5. Две батареи: полная и ~50 % (пункт 4.2).
6. Площадки: стол/подставка, ровно 5×5 м, скользкое (плитка, линолеум),
   уклон 10–15°, мерная лента 5–10 м.

**Разбор логов:**

```bash
python3 tools/analyze_telemetry.py log.csv [--no-plots]   # покой, частоты, здоровье лога
python3 tools/trial_metrics.py log.csv --only <метрика>   # yaw-step | gyro-bias | failsafe | kids | ekf-dist
python firmware/sim/validate_logs.py log.csv [--plot out.png]   # подгонка физмодели SIL по заезду
```

С `--only` метрика без нужных данных считается FAIL, без него — SKIP.

**SIL-эталоны:**

```bash
cd firmware/tests && cmake -B build && cmake --build build   # собирает sim_host
cd ../sim && python -m pytest tests/<файл>::<тест>
```

---

## 1. Bring-up (стол, колёса в воздухе)

| № | Проверка | PASS | Лог | Разбор | SIL | Итог |
|---|---|---|---|---|---|---|
| 1.1 | Wi-Fi и web UI (IC D1#1) | WebSocket `Подключено` | — | — | — | [ ] |
| 1.2 | Self-test (IC D1#2) | все 10 проверок зелёные | — | — | GTest `test_self_test.cpp` | [ ] |
| 1.3 | Частота цикла (IC D1#3) | `DIAG: loop=500 Hz` (≥ 490) | serial, 1 мин PSRAM | `analyze_telemetry.py` (loop rate, cadence 10 мс, срывов ≤ 0.5 %) | — | [ ] |
| 1.4 | Калибровка gyro, затем full (IC D1#4–5) | status=done, bias < 5 dps; full: g ≈ [0, 0, 1] | события ImuCalib* | — | — | [ ] |
| 1.5 | Калибровка переживает ребут (LOS-227) | после перезагрузки `calib_valid` в self-test, повторной калибровки не просит | — | — | GTest | [ ] |
| 1.6 | Конфиг после прошивки (LOS-171, миграции NVS v13/v14) | ручной тюнинг сохранён, нетронутые пресеты Normal/Sport получили новые коэффициенты | `get_stab_config` / web UI | — | GTest миграций | [ ] |
| 1.7 | Знак pitch: нос вверх ~30° (IC D1#6, **LOS-369 п. 1**) | **pitch ≈ −30°** (нос вверх — отрицательный) | web UI / CSV `pitch_deg` | вручную | `test_testing_program_scenarios.py::test_slope_is_seen_as_pitch` | [ ] |
| 1.8 | Roll и yaw (IC D1#7–8) | наклон 30° → roll ≈ 30°; поворот 90° → Δyaw ≈ 90° | web UI | вручную | — | [ ] |
| 1.9 | Газ и руль в воздухе (IC D1#13–14) | колёса крутятся в нужную сторону, руль влево/вправо | — | визуально | — | [ ] |
| 1.10 | Kids preset в покое (IC D1#12) | Toddler: `throttle_limit` 0.15 в телеметрии | WS | — | — | [ ] |
| 1.11 | CSV из PSRAM (IC D1#16) | открывается, предупреждений о колонках нет | 1 мин PSRAM | `analyze_telemetry.py` | — | [ ] |

## 2. Потеря связи и failsafe (стол)

| № | Проверка | PASS | Лог | Разбор | SIL | Итог |
|---|---|---|---|---|---|---|
| 2.1 | Потеря RC и Wi-Fi (TP Ф0, IC D1#10, D4#3): **два отдельных прогона**, другой источник выключен (failsafe — только когда пропали оба, RC в приоритете). (а) RC: браузер закрыт, газ ~0.2 и руль с пульта, выключить пульт; (б) Wi-Fi: пульт выключен, газ ~0.2 и руль слайдерами, закрыть браузер. Колёса подняты | моторы в нейтрали **≤ 250 мс**; в выводе нет пометки «машина уже стояла» | PSRAM: `rc_ok, wifi_ok, failsafe_active, throttle, steering` (буфер переживает закрытие браузера — CSV скачать после) | `trial_metrics.py loss.csv --only failsafe` | `test_testing_program_scenarios.py::test_rc_loss_neutral_within_250_ms_and_recovers`, `::test_wifi_loss_neutral_within_250_ms` | [ ] RC ___ мс, Wi-Fi ___ мс |
| 2.2 | Восстановление (IC D1#11) | failsafe снят, управление вернулось | тот же | тот же | тот же rc_loss (≤ 60 мс) | [ ] |
| 2.3 | То же в Kids (LOS-171) | как 2.1 | тот же | тот же | — | [ ] ___ мс |
| 2.4 | Приоритет RC над Wi-Fi (IC D4#1–2, LOS-162) | RC перехватывает управление без рывка; без RC ведёт Wi-Fi, failsafe нет | PSRAM `throttle, steering, rc_ok` | вручную по графику | — | [ ] |
| 2.5 | Видимость потерь RC (**LOS-216**): выключить пульт, вынуть приёмник, держать стик у упора в Kids | каждое выключение → одно событие `RcLost` (21) с причиной и шириной импульса, `RcRestored` (22) с длительностью; `RC Losses` в UI +1; serial `RC LOST` / `RC restored`. Стик у упора — потерь нет, `rc_throttle/rc_steering` не нулевые | PSRAM (колонки `event_*`), serial | вручную по `event_type` | `test_rc_link.py::test_receiver_dropout_reported_with_cause_and_duration`, `::test_stick_at_end_stop_keeps_rc_in_kids_mode`, `::test_out_of_range_pulses_reported_with_width` | [ ] |

Пауза Wi-Fi короче таймаута (80–160 мс без failsafe) на железе не задаётся —
UI шлёт команды каждые 20 мс, инструмента для паузы заданной длины нет.
Проверяется только SIL: `::test_wifi_command_gap_does_not_trip_failsafe_below_timeout`.

## 3. Покой (стол, без вибраций)

Одна запись покрывает 3.1–3.3: калибровка gyro → «Очистить» → **8 мин** не трогать → CSV.

| № | Проверка | PASS | Лог | Разбор | SIL | Итог |
|---|---|---|---|---|---|---|
| 3.1 | Дрейф bias gyro (TP Ф0) | < 0.5 °/с за минуту (порог ориентировочный, уточнить по этой записи) | PSRAM 8 мин | `trial_metrics.py rest.csv --only gyro-bias` | `test_trial_metrics.py::test_gyro_bias_on_sil_rest_log` | [ ] ___ °/с/мин |
| 3.2 | Madgwick без дрейфа (RM Ф0) | pitch/roll < 1 °/мин, std < 1° | тот же | `gyro-bias` + `analyze_telemetry.py` | — | [ ] |
| 3.3 | EKF и ZUPT в покое (IC D1#9, D5#3–4, 7) | \|vx\|, \|vy\| < 0.05 м/с; \|slip\| < 1°; std(gz) < 2 dps; \|a\| в [0.95, 1.05] g | тот же | `analyze_telemetry.py` (его порог ZUPT 0.1 мягче — смотреть 0.05) | `test_plausibility.py::test_coast_without_command_stays_stopped` | [ ] |
| 3.4 | Baseline шумов (TP Ф0) | записан; PASS нет | тот же | `analyze_telemetry.py` (std по осям); шумовая модель SIL подгоняется только по логам с движением: `validate_logs.py --fit-road-noise --road-logs *_7.0_*.csv` (нужны скорости ≥ 0.5 м/с в нескольких диапазонах) | — | [ ] |

## 4. ESC и мёртвая зона (подставка → пол)

| № | Проверка | PASS | Лог | Разбор | SIL | Итог |
|---|---|---|---|---|---|---|
| 4.1 | Поведение ESC (IC D3#1–7) | таблица наблюдений заполнена (coast/brake, прямой реверс, пауза через нейтраль 300 мс / 1 с, тормозной газ) | — | — | — | [ ] |
| 4.2 | Минимальный газ старта (LOS-169) на полной и ~50 % батарее | найден порог: газ ступенями 0.02 → 0.20 через 0.01, по 2 с; порог — первая ступень, на которой машина трогается (видно глазом, `ax` > 0.03 g). **Мотор-модель на время теста выключить** (`motor_model_enabled` = false, после — вернуть): иначе EKF подставляет её скорость и `speed_ms` растёт и без движения | PSRAM `throttle, cmd_throttle, ax, gx..gz`; видео | вручную (скрипта нет) | `test_vehicle_model.py::test_deadzone_no_motion` (в SIL `motor_deadzone` = 0.05 — допущение, заменить измерением) | [ ] ___ / ___ |

## 5. IMU SPI выше 500 кГц (стенд, LOS-255)

Отдельная сборка на каждую частоту: `IMU_SPI_BAUD_HZ` в `firmware/esp32_s3/main/config.hpp` (сейчас
500000), затем из `firmware/esp32_s3` `idf.py -DRC_PROFILE_LOOP=1 build flash monitor` — каждую
частоту прошить, прежде чем мерить. Короткие провода. **После этапа вернуть 500000 и прошить
`make build && make flash`** — полевые этапы идут на базовой частоте `develop`.

| № | Проверка | PASS | Лог | Разбор | SIL | Итог |
|---|---|---|---|---|---|---|
| 5.1 | 1, 2, 4, 8 МГц по очереди | WHO_AM_I читается; `DIAG: loop=500 Hz`; std(gz) и \|a\| в покое не хуже, чем на 500 кГц; `spi_avg` падает (500 кГц ≈ 415 мкс) | serial `PROF(imu us)`, 1 мин PSRAM на каждой частоте | `analyze_telemetry.py` (сравнить с 500 кГц) | — (GTest `test_mpu6050_spi.cpp`) | [ ] макс. ___ МГц |

Счётчика ошибок чтения IMU в прошивке нет: неудачный семпл молча пропускается.
Поэтому «нет сбойных семплов» сейчас проверяется только косвенно — по частоте
цикла и шуму.

## 6. Датчик Холла — заблокировано

Не реализован (LOS-238, закупка LOS-239): нет поля в логе, скрипта и SIL.
Пока вместо него — пункт 7.9 (EKF против мерной ленты).

## 7. Полевые испытания

Перед этапом: батарея полная, площадка 5×5 м, пульт RC для 7.1. 7.0 — в DirectLaw;
для 7.1–7.4 **включить стабилизацию** (по умолчанию выключена; в логе `stab_enabled` = 1),
иначе PID не работает и 7.1 даст ложный FAIL.

| № | Проверка | PASS | Лог | Разбор | SIL | Итог |
|---|---|---|---|---|---|---|
| 7.0 | Базовое движение (TP Ф1, IC D2): прямая 5 м на 30 % (DirectLaw), стоп, задний ход, повороты на месте | прямо: vx > 0, vy ≈ 0, курс не уходит; стоп: ZUPT vx → 0 за 1 с; назад: vx < 0, slip ≈ 0; на месте: yaw_rate гироскопа совпадает по знаку с рулём | PSRAM | `analyze_telemetry.py` | `test_plausibility.py`, `test_scenario_replay.py::test_marked_straight_does_not_drift`, `test_closed_loop.py::test_step_steer_turns_symmetric` | [ ] |
| 7.1 | Yaw-rate PID, шаг руля 0.3–0.5 на газе 0.4 (TP Ф4; **LOS-366** Normal, **LOS-371** Sport) | **overshoot < 20 %, settling < 200 мс** (полоса ±10 %), без раскачки | PSRAM, **управление с пульта** (по Wi-Fi `rc_steering` не пишется) | Normal: `trial_metrics.py run.csv --only yaw-step`; Sport: `... --gain-dps 120` | `test_testing_program_scenarios.py -k "yaw_rate_step and normal"` / `-k "yaw_rate_step and sport"` (overshoot, settling, без limit cycle; SIL: Normal ≤ 7.3 % / 188 мс, Sport ≤ 2 % / 150 мс) | [ ] N ___ / ___ · S ___ / ___ |
| 7.2 | Склон 10–15°, pitch comp (TP Ф4; **LOS-369 п. 2**): заезд вверх и вниз с компенсацией и без | при том же стике (`rc_throttle`) `cmd_throttle` (команда после стабилизации) с комп. выше, чем без, больше чем на 0.05, и скорость выше; вниз: `cmd_throttle` меньше; на ровном разница < 0.02 | PSRAM `pitch_deg, rc_throttle, cmd_throttle, speed_ms`, управление с пульта | вручную (скрипта нет) | `::test_pitch_comp_adds_throttle_uphill`, `::test_pitch_comp_cuts_throttle_downhill`, `::test_pitch_comp_idle_on_flat`, `::test_pitch_comp_no_feedback_from_flat_acceleration` | [ ] |
| 7.3 | Oversteer guard, Normal: резкий газ в повороте (TP Ф4), **управление с пульта** | `oversteer_active` = 1 во время заноса, `cmd_throttle` (после guard) ниже стика `rc_throttle`, max \|slip\| меньше, чем при reduction = 0 | PSRAM `oversteer_active, slip_deg, yaw_rate_dps, rc_throttle, cmd_throttle, throttle` | вручную | `::test_oversteer_guard_cuts_throttle_and_slip`, `test_scenario_replay.py::test_recorded_oversteer_episode_triggers_guard` | [ ] |
| 7.4 | Drift, slip-angle PID на скользком (TP Ф4, RM Ф3), `target_deg` 15° | устойчивый управляемый занос около уставки; угол сверить с видео сверху (EKF на скользком сильно занижает slip, LOS-370) | PSRAM `slip_deg, yaw_rate_dps, vx, vy`; видео | вручную | `::test_drift_slip_target_shifts_throttle_correction` (только знак) | [ ] |
| 7.5a | Kids, пресеты Toddler/Child/Preteen (TP Ф5, LOS-171) | газ не выше 0.15 / 0.30 / 0.50, руль 0.5 / 0.7 / 0.85; скорость растёт по возрасту; записать max скорость | PSRAM (`drive_mode` = 3, `kids_*`, `rc_throttle`, `throttle`, `steering`) | вручную: при полном стике max `throttle` и \|`steering`\| не выше лимита пресета; `trial_metrics.py kids.csv --only kids` — только пиковые `speed_ms` и `ax` для записи | `::test_kids_preset_caps_full_stick_throttle[...]`, `::test_kids_presets_order_by_age` | [ ] ___ / ___ / ___ м/с |
| 7.5b | Kids accel limiter: резкий газ с нуля | `kids_accel_limit_active` = 1, разгон плавнее, чем в Normal; лимитер не дёргается на ровной дороге | тот же, плюс такой же старт в Normal | вручную: `kids_accel_limit_active` = 1 на старте, рост `throttle` медленнее, чем в Normal; `trial_metrics.py kids.csv --only kids` — пик `ax` для записи (завышен наклоном и вибрацией) | `::test_kids_accel_limiter_smooths_launch` | [ ] |
| 7.5c | Kids anti-spin: скользко, газ в пол | **ожидаемо FAIL**: в SIL anti-spin не видит ни пробуксовку, ни занос (strict xfail, LOS-370). Цель — записать лог для будущего сигнала пробуксовки | PSRAM `kids_anti_spin_active, slip_deg, speed_ms, ax, throttle` | — | `test_kids_anti_spin.py` (xfail) | [ ] лог записан |
| 7.5d | Normal ↔ Kids в движении, failsafe в Kids (LOS-171) | переключение без рывка газа | PSRAM | вручную | — | [ ] |
| 7.5e | Ребёнок (или имитация) в каждом пресете (TP Ф5) | субъективно: предсказуемо и безопасно | — | — | — | [ ] |
| 7.6 | Калибровка rx/ry CoM по кругу (TP Ф2): web UI «калибровка CoM», CW и CCW, площадка ≥ 3×3 м | калибровка `valid`; \|rx\|, \|ry\| ≤ 0.3 м; на круге после коррекции slip ближе к нулю, чем до | PSRAM до и после (`ax, ay, gz, slip_deg`), `get_com_offset_status` | вручную | — (GTest `test_com_offset_calibration.cpp`) | [ ] rx ___ ry ___ |
| 7.7 | Madgwick β = 0.05 / 0.1 / 0.2 (TP Ф3) | минимальный β, при котором pitch/roll успевают за манёвром (нет запаздывания на склоне и в повороте) | PSRAM на каждый β | вручную | — | [ ] β = ___ |
| 7.8 | LPF cutoff, грунт против асфальта (TP Ф3), default 30 Гц | вибрация подвески в `yaw_rate_dps` (gz после LPF) подавлена, шаг yaw rate (7.1) не хуже | PSRAM, одинаковый проезд на каждом cutoff | вручную (скрипта нет): спектр `yaw_rate_dps` против сырого `gz` — мощность выше ~10 Гц падает с cutoff; overshoot/settling `trial_metrics.py cutoff.csv --only yaw-step` не хуже, чем на 30 Гц | — | [ ] ___ Гц |
| 7.9 | EKF против мерной ленты 5–10 м (TP Ф3) | дистанция по `speed_ms` в пределах ±5 % | PSRAM; старт и финиш из покоя, участок `--from-ms/--to-ms` — от роста `speed_ms` до возврата к 0 | `trial_metrics.py run.csv --only ekf-dist --distance-m 5.0 --from-ms <t0> --to-ms <t1>` (t0, t1 — `ts_ms` начала и конца участка) | `test_trial_metrics.py::test_ekf_distance_error` | [ ] ___ % |

Oversteer guard в Normal **по умолчанию выключен** (`warn_enabled=false`,
`throttle_reduction=0`). Перед 7.3 включить его в web UI и задать reduction,
например 0.7 как в Kids, иначе тест ничего не проверит. Пороги Normal
(slip 20°, rate 50 °/с) выше, чем в SIL-тесте (5°, 10 °/с), а EKF занижает slip
в заносе — снизить их, иначе `oversteer_active` может не включиться вовсе. Пункт IC D1#15 до этого
проходит тривиально.

---

## Итог сессии

| Этап | PASS / всего | Блокеры |
|---|---|---|
| 1. Bring-up | ___ / 11 | |
| 2. Связь и failsafe | ___ / 5 | |
| 3. Покой | ___ / 4 | |
| 4. ESC | ___ / 2 | |
| 5. SPI | ___ / 1 | |
| 7. Полевые | ___ / 14 | |

После сессии: логи — в задачи, подтверждённые пункты закрыть (LOS-216, 227,
255, 366, 369, 371), пороги 3.1 и 4.2 обновить в SIL и в этом документе.

## Чего не хватает (заводить отдельно)

- `tools/udp_telem.py` и `docs/udp-telemetry-design.md` описывают кадр 80 байт,
  прошивка шлёт 132.
- Скрипты разбора: порог старта ESC (4.2), pitch comp (7.2), oversteer (7.3),
  rx/ry до и после (7.6), β (7.7), cutoff (7.8), события RcLost/RcRestored (2.6).
- Счётчик ошибок чтения IMU для 5.1.
- SIL: флаги `sim_host` для β, cutoff и CoM offset; слежение за заносом в Drift.
- Kids anti-spin: сигнал пробуксовки, независимый от slip EKF (LOS-370).
- Датчик Холла (LOS-238).
