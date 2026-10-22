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
4. **UDP-запись** (`tools/udp_telem.py record`) даёт те же колонки, что CSV
   из PSRAM, но без событий (`event_*`) и с потерями пакетов по Wi-Fi.
   Основной лог — PSRAM; UDP — для записей длиннее 8.7 мин.
5. Две батареи: полная и ~50 % (пункт 4.2).
6. Площадки: стол/подставка, ровно 5×5 м, скользкое (плитка, линолеум),
   уклон 10–15°, поперечный склон 5–10° (7.2b), мерная лента 5–10 м.

**Разбор логов:**

```bash
python3 tools/analyze_telemetry.py log.csv [--no-plots]   # покой, частоты, здоровье лога
python3 tools/trial_metrics.py log.csv --only <метрика>   # yaw-step | gyro-bias | failsafe | kids | ekf-dist
                                                          # esc-start | pitch-comp | oversteer | rc-events
                                                          # com-offset | madgwick-beta | lpf
                                                          # imu-errors | grip-fit
python3 tools/trial_metrics.py log.csv --only lpf --baseline ref.csv   # сравнить с другим логом (7.6–7.8)
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
| 1.5 | Калибровка переживает ребут (LOS-227): `make monitor` открыт ещё во время 1.4 | после 1.4 в serial `Saved calib: …` (без `nvs_set_blob/commit failed`); после ребута `IMU calibration loaded from NVS` и строка `Loaded calib: …` с теми же значениями, что в `Saved calib`; нет `Saved forward axis rejected`. Self-test и значения в web UI не доказывают: на старте сразу идёт автокалибровка, она делает `calib_valid` зелёным и перезаписывает значения | — | — | GTest | [ ] |
| 1.6 | Конфиг после прошивки (LOS-171, миграции NVS v13/v14) | ручной тюнинг сохранён, нетронутые пресеты Normal/Sport получили новые коэффициенты | `get_stab_config` / web UI | — | GTest миграций | [ ] |
| 1.7 | Знак pitch: нос вверх ~30° (IC D1#6, **LOS-369 п. 1**) | **pitch ≈ −30°** (нос вверх — отрицательный) | web UI / CSV `pitch_deg` | вручную | `test_testing_program_scenarios.py::test_slope_is_seen_as_pitch` | [ ] |
| 1.8 | Roll и yaw (IC D1#7–8) | наклон 30° → roll ≈ 30°; поворот 90° → Δyaw ≈ 90° | web UI | вручную | — | [ ] |
| 1.9 | Газ и руль в воздухе (IC D1#13–14) | колёса крутятся в нужную сторону, руль влево/вправо | — | визуально | — | [ ] |
| 1.9a | Газ не проходит, пока идёт калибровка при старте (LOS-380): включить питание и сразу дать газ | колёса стоят, пока web UI показывает calib `collecting` (до 2 с после старта), и крутятся после `done`; руль отвечает сразу | WS `calib.status` | визуально | GTest `ControlLoopTest.BootCalibrationHoldsThrottle`, `ProcessorTest.RestCalibration_*` | [ ] |
| 1.10 | Kids preset в покое (IC D1#12): кнопка «Toddler» в блоке Kids web UI | Toddler: `throttle_limit` 0.15 в телеметрии | WS | — | — | [ ] |
| 1.11 | CSV из PSRAM (IC D1#16) | открывается, предупреждений о колонках нет | 1 мин PSRAM | `analyze_telemetry.py` | — | [ ] |

## 2. Потеря связи и failsafe (стол)

| № | Проверка | PASS | Лог | Разбор | SIL | Итог |
|---|---|---|---|---|---|---|
| 2.1 | Потеря RC и Wi-Fi (TP Ф0, IC D1#10, D4#3): **два отдельных прогона**, другой источник выключен (failsafe — только когда пропали оба, RC в приоритете). (а) RC: браузер закрыт, газ ~0.2 и руль с пульта, выключить пульт; (б) Wi-Fi: пульт выключен, газ ~0.2 и руль слайдерами, закрыть браузер. Колёса подняты | моторы в нейтрали **≤ 250 мс**; в выводе нет пометки «машина уже стояла» | PSRAM: `rc_ok, wifi_ok, failsafe_active, throttle, steering` (буфер переживает закрытие браузера — CSV скачать после) | `trial_metrics.py loss.csv --only failsafe` | `test_testing_program_scenarios.py::test_rc_loss_neutral_within_250_ms_and_recovers`, `::test_wifi_loss_neutral_within_250_ms` | [ ] RC ___ мс, Wi-Fi ___ мс |
| 2.2 | Восстановление (IC D1#11): в каждом прогоне 2.1 вернуть источник (включить пульт / открыть браузер) и снова дать газ ~0.2 и руль | `failsafe_active` = 0 и `throttle`, `steering` снова ненулевые, равны новой команде | тот же | вручную: в CSV после возврата `rc_ok`/`wifi_ok` | тот же rc_loss (≤ 60 мс) | [ ] |
| 2.3 | То же в Kids (LOS-171) | как 2.1 | тот же | тот же | — | [ ] ___ мс |
| 2.4 | Приоритет RC над Wi-Fi (IC D4#1–2, LOS-162) | RC перехватывает управление без рывка; без RC ведёт Wi-Fi, failsafe нет | PSRAM `throttle, steering, rc_ok` | вручную по графику | — | [ ] |
| 2.5 | Видимость потерь RC (**LOS-216**): выключить пульт, вынуть приёмник, держать стик у упора в Kids | каждое выключение → одно событие `RcLost` (21) с причиной и шириной импульса, `RcRestored` (22) с длительностью; `RC Losses` в UI +1; serial `RC LOST` / `RC restored`. Стик у упора — потерь нет, `rc_throttle/rc_steering` не нулевые | PSRAM (колонки `event_*`), serial | `trial_metrics.py rc.csv --only rc-events`: каждая потеря с причиной, шириной импульсов и длительностью; число потерь сверить с числом выключений | `test_rc_link.py::test_receiver_dropout_reported_with_cause_and_duration`, `::test_stick_at_end_stop_keeps_rc_in_kids_mode`, `::test_out_of_range_pulses_reported_with_width` | [ ] |

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
| 4.2 | Минимальный газ старта (LOS-169) на полной и ~50 % батарее | найден порог: газ ступенями 0.02 → 0.20 через 0.01, по 2 с; порог — первая ступень, на которой машина трогается (видно глазом, `ax` > 0.03 g). **Мотор-модель на время теста выключить** (галочка «Мотор-модель в EKF» в web UI, «Сохранить»; после — вернуть): иначе EKF подставляет её скорость и `speed_ms` растёт и без движения | PSRAM `throttle, cmd_throttle, ax, gx..gz`; видео | `trial_metrics.py esc.csv --only esc-start` (порог по применённому `throttle`, прирост √(ax²+ay²) > 0.03 g); сверить с видео | `test_vehicle_model.py::test_deadzone_no_motion` (в SIL `motor_deadzone` = 0.05 — допущение, заменить измерением) | [ ] ___ / ___ |

## 5. IMU SPI выше 500 кГц (стенд, LOS-255)

Отдельная сборка на каждую частоту: `IMU_SPI_BAUD_HZ` в `firmware/esp32_s3/main/config.hpp` (сейчас
500000), затем из `firmware/esp32_s3` `idf.py -DRC_PROFILE_LOOP=1 build flash monitor` — каждую
частоту прошить, прежде чем мерить. Короткие провода. **После этапа вернуть 500000, сбросить
кэш сборки с `RC_PROFILE_LOOP` и прошить из `projects/rc_vehicle` (не из `firmware/esp32_s3`):
`make clean && make build && make flash`** — полевые этапы идут на базовой частоте `develop`.

| № | Проверка | PASS | Лог | Разбор | SIL | Итог |
|---|---|---|---|---|---|---|
| 5.1 | 1, 2, 4, 8 МГц по очереди | WHO_AM_I читается; `DIAG: loop=500 Hz`; **ошибок чтения IMU 0 за минуту** (`imu-errors`, в serial `IMU err: read=0 nan=0 who=0 run=0`); std(gz) и \|a\| в покое не хуже, чем на 500 кГц; `spi_avg` падает (500 кГц ≈ 415 мкс) | serial `PROF(imu us)` и `IMU err`, ≥ 70 с PSRAM на каждой частоте | `trial_metrics.py log.csv --only imu-errors`; `analyze_telemetry.py` (сравнить с 500 кГц) | — (GTest `test_mpu6050_spi.cpp`, `test_imu_handler.cpp`) | [ ] макс. ___ МГц |

Ошибки чтения IMU (LOS-383) считает `ImuHandler`: неудачное чтение (ошибка SPI
или «пустая шина» — все байты ответа 0x00/0xFF), семпл с NaN/Inf, несовпадение
WHO_AM_I при перепроверке раз в секунду и самая длинная серия пропусков подряд.
Счётчики с момента старта печатаются в serial строкой `IMU err:` вместе с `DIAG:`,
а раз в 10 с (в том числе с нулями) пишутся в журнал событием `ImuReadStats`:
param — несовпадения WHO_AM_I, value1 — пропущенные семплы, value2 — самая
длинная серия за период. `imu-errors` складывает события лога и требует покрытия
не меньше минуты: первое событие приходит через 10 с после старта, поэтому лог
пишется не короче 70 с. Насыщение отдельной оси ошибкой не считается: при ±2 g
удар о кочку законно упирает акселерометр в предел.

## 6. Датчик Холла — заблокировано

Не реализован (LOS-238, закупка LOS-239): нет поля в логе, скрипта и SIL.
Пока вместо него — пункт 7.9 (EKF против мерной ленты).

## 7. Полевые испытания

Перед этапом: батарея полная, площадка 5×5 м, пульт RC для 7.1–7.3 и 7.8. 7.0 — в DirectLaw;
для 7.1–7.4 **включить стабилизацию** (по умолчанию выключена; в логе `stab_enabled` = 1),
иначе PID не работает и 7.1 даст ложный FAIL.

| № | Проверка | PASS | Лог | Разбор | SIL | Итог |
|---|---|---|---|---|---|---|
| 7.0 | Базовое движение (TP Ф1, IC D2): прямая 5 м на 30 % (DirectLaw), стоп, задний ход, повороты на месте | прямо: vx > 0, vy ≈ 0, курс не уходит; стоп: ZUPT vx → 0 за 1 с; назад: vx < 0, slip ≈ 0; на месте: yaw_rate гироскопа совпадает по знаку с рулём | PSRAM | `analyze_telemetry.py` | `test_plausibility.py`, `test_scenario_replay.py::test_marked_straight_does_not_drift`, `test_closed_loop.py::test_step_steer_turns_symmetric` | [ ] |
| 7.1 | Yaw-rate PID, шаг руля 0.3–0.5 на газе 0.4 (TP Ф4; **LOS-366** Normal, **LOS-371** Sport) | **overshoot < 20 %, settling < 200 мс** (полоса ±10 %), без раскачки | PSRAM, **управление с пульта** (по Wi-Fi `rc_steering` не пишется) | Normal: `trial_metrics.py run.csv --only yaw-step`; Sport: `... --gain-dps 120` | `test_testing_program_scenarios.py -k "yaw_rate_step and normal"` / `-k "yaw_rate_step and sport"` (overshoot, settling, без limit cycle; SIL после исправления Q LPF gz (LOS-380): Normal ≤ 8.1 % / 162 мс, Sport ≤ 2 % / 138 мс) | [ ] N ___ / ___ · S ___ / ___ |
| 7.2 | Склон 10–15°, pitch comp (TP Ф4; **LOS-369 п. 2**): заезд вверх и вниз с компенсацией и без | при том же стике (`rc_throttle`) `cmd_throttle` (команда после стабилизации) с комп. выше, чем без, больше чем на 0.05, и машина заезжает (глазом/видео; `speed_ms` не годится — с мотор-моделью она считается из газа); вниз: `cmd_throttle` меньше; на ровном разница < 0.02 | PSRAM `pitch_deg, rc_throttle, cmd_throttle, speed_ms`, управление с пульта | `trial_metrics.py slope.csv --only pitch-comp`: добавка к газу (`cmd_throttle` минус стик после мёртвой зоны RC 8 %) в гору, под гору и на ровном; держать ровный газ, окна с неустоявшимся тангажем не считаются. Прогон без компенсации — та же метрика, добавка ≈ 0 | `::test_pitch_comp_adds_throttle_uphill`, `::test_pitch_comp_cuts_throttle_downhill`, `::test_pitch_comp_idle_on_flat`, `::test_pitch_comp_no_feedback_from_flat_acceleration`; скрипт — `test_trial_metrics.py::test_pitch_comp_on_sil_climb` | [ ] |
| 7.2b | Поперечный склон 5–10°, roll comp (**LOS-386**, по умолчанию выключена): прямой проезд поперёк склона на газе ~0.4, руль пульта в нуле, правым и левым бортом вниз. Сначала в Normal со стабилизацией без компенсации — подобрать `gain`, затем в Drift (стабилизация включена) без и с компенсацией (галочка «Roll compensation» и Gain в web UI, «Сохранить»). В снапшотах конфигурации лога roll_comp нет: какие прогоны с компенсацией и какой `gain`, записать вручную | Normal: на склоне `steering` ≠ `rc_steering` (держит yaw-ПИД), `gain` = среднее (`steering` − `rc_steering`) / `roll_deg` по прямому участку. Drift: с компенсацией при правом борте ниже (`roll_deg` > 0) `steering` > 0 (влево, вверх по склону), при левом — < 0; снос вниз по склону за 5 м меньше, чем без неё (рулетка/видео); на ровном `steering` = `rc_steering` | PSRAM `roll_deg, rc_steering, steering, yaw_rate_dps, speed_ms`, управление с пульта | вручную: средние по прямому участку; `roll_deg` в логе от Madgwick, на прямой с постоянной скоростью он совпадает с креном TiltEstimator, по которому работает компенсация | `::test_bank_is_seen_as_roll_and_turns_car_downhill`, `::test_roll_comp_steers_uphill_and_holds_heading[...]`, `::test_roll_comp_idle_on_flat`; GTest `test_roll_compensator.cpp` | [ ] gain = ___ |
| 7.3 | Oversteer guard, Normal: резкий газ в повороте (TP Ф4), **управление с пульта** | `oversteer_active` = 1 во время заноса, `cmd_throttle` (после guard) ниже стика (`rc_throttle` после мёртвой зоны RC 8 %: стик 0.5 даёт 0.457 и без guard), max \|slip\| меньше, чем при reduction = 0 | PSRAM `oversteer_active, slip_deg, yaw_rate_dps, rc_throttle, cmd_throttle, throttle` | `trial_metrics.py os.csv --only oversteer`: по эпизодам — насколько срезан газ и max \|slip\|; прогон с reduction = 0 даёт FAIL по газу (так и должно), сравнить max \|slip\| | `::test_oversteer_guard_cuts_throttle_and_slip`, `test_scenario_replay.py::test_recorded_oversteer_episode_triggers_guard`; скрипт — `test_trial_metrics.py::test_oversteer_on_sil_corner` | [ ] |
| 7.4 | Drift, slip-angle PID на скользком (TP Ф4, RM Ф3), `target_deg` 15° | устойчивый управляемый занос около уставки; угол сверить с видео сверху (EKF на скользком сильно занижает slip, LOS-370) | PSRAM `slip_deg, yaw_rate_dps, vx, vy`; видео | вручную | `::test_drift_slip_target_shifts_throttle_correction` (только знак); удержание уставки — `::test_drift_holds_slip_target` (strict xfail: уставка знаковая, а EKF в силовом заносе видит единицы градусов, см. LOS-380) | [ ] |
| 7.5a | Kids, пресеты Toddler/Child/Preteen (TP Ф5, LOS-171), кнопки пресетов в блоке Kids web UI; на замер лимитов стабилизацию выключить (`enabled` = false): иначе yaw-контур после лимита Kids законно уводит `steering` выше лимита | газ не выше 0.15 / 0.30 / 0.50, руль 0.5 / 0.7 / 0.85; скорость растёт по возрасту — по времени проезда 5 м на полном стике (секундомер или видео; `speed_ms` не годится — с мотор-моделью она считается из газа) | PSRAM (`drive_mode` = 3, `kids_*`, `rc_throttle`, `throttle`, `steering`) | вручную: при полном стике max `throttle` и \|`steering`\| не выше лимита пресета; `trial_metrics.py kids.csv --only kids` — пиковые `speed_ms` и `ax` только для справки | `::test_kids_preset_caps_full_stick_throttle[...]`, `::test_kids_presets_order_by_age` | [ ] ___ / ___ / ___ с на 5 м |
| 7.5b | Kids accel limiter: резкий газ с нуля, два старта в одном пресете — с `accel_limit_enabled` и без: галочка «Ограничение ускорения» в блоке Kids web UI, затем «Сохранить» (остальное как есть; кнопка пресета снова включает лимитер, поэтому галочку снимать после выбора пресета) | `kids_accel_limit_active` = 1 на старте; пока он = 1, `throttle` ниже, чем в старте без лимитера в тот же момент от начала стика (пик `throttle` не годится: после разгона оба старта упираются в один лимит пресета); лимитер не дёргается на ровной дороге | тот же, оба старта | вручную: оба лога выровнять по моменту, когда `rc_throttle` уходит от 0, и сравнить `throttle` в окне, где `kids_accel_limit_active` = 1 (сравнивать с Normal нельзя — у пресетов свой slew); `trial_metrics.py kids.csv --only kids` — пик `ax` для записи (завышен наклоном и вибрацией) | `::test_kids_accel_limiter_smooths_launch` | [ ] |
| 7.5c | Kids anti-spin по пробуксовке (LOS-385): старт по прямой на скользком (газ в пол), затем тот же старт на асфальте; трогаться не раньше 10 с после включения — до схождения наклона детектор молчит | скользко: `kids_mode.wheelspin_active` = 1 на старте, `throttle` срезан, колёса буксуют меньше (видео); асфальт: `wheelspin_active` всё время 0, `wheel_slip_ratio` ниже 0.12 (порог `kWheelspinOnRatio`) | WS-телеметрия `kids_mode.wheelspin_active, kids_mode.wheel_slip_ratio, throttle`; PSRAM `kids_anti_spin_active, speed_ms, ax, throttle` | вручную: пик `wheel_slip_ratio` на асфальте — запас до порога | `test_kids_anti_spin.py::test_anti_spin_cuts_wheelspin_on_ice_launch`, `::test_no_wheelspin_on_asphalt` | [ ] асфальт: пик ___ |
| 7.5d | Normal ↔ Kids в движении, failsafe в Kids (LOS-171) | переключение без рывка газа | PSRAM | вручную | — | [ ] |
| 7.5e | Ребёнок (или имитация) в каждом пресете (TP Ф5) | субъективно: предсказуемо и безопасно | — | — | — | [ ] |
| 7.5f | Признак потери сцепления (LOS-378): круги на асфальте в Normal с рулём 0.3, 0.6 и 1.0, на каждом руле скорость медленно от шага до полной; затем тот же круг на скользком | асфальт: `ekf.grip_lost` = 0 на установившемся круге, `ekf.slip_deg` ниже 10°; скользко: `grip_lost` = 1, `slip_deg` растёт вместе с заносом (видео). Если на асфальте `grip_lost` = 1 на скорости, `yaw_rate` ниже эталона v·tan(руль·20.4°)/0.30: подогнать `kUndersteerGradient` и `kMaxSteerRad` в `grip_loss_detector.hpp`; если `grip_lost` = 1 только на входе в поворот — увеличить `kYawResponseTauS` | PSRAM `steering, vx, yaw_rate_dps` (асфальт и скользко — отдельные логи); WS-телеметрия `ekf.grip_lost, ekf.slip_deg` | `trial_metrics.py asphalt.csv --only grip-fit`: доля окон, где с константами прошивки был бы ложный `grip_lost` (≤ 5 %), и подгонка `kMaxSteerRad` и `kUndersteerGradient` (K) по установившимся участкам кругов; при FAIL — вписать подогнанные значения в `common/grip_loss_detector.hpp`, перепрошить и повторить. Лог со скользкого для подгонки не годится | `test_kids_anti_spin.py::test_no_false_slip_on_asphalt`, `::test_anti_spin_triggers_early_on_slippery_turn`; скрипт — `test_trial_metrics.py::test_grip_fit_on_sil_understeer_car`, `::test_grip_fit_on_sil_neutral_car` | [ ] K = ___ |
| 7.5g | Kids, руль по сцеплению (LOS-378): на скользком полный руль под газом в пол, затем тот же поворот на асфальте с резким входом | скользко: `kids_mode.grip_steer_limit_active` = 1, `steering` ниже команды, машина идёт по дуге без заноса (видео), `ekf.slip_deg` ниже 10°; асфальт: `grip_steer_limit_active` всё время 0, в том числе на входе в поворот | WS-телеметрия `kids_mode.grip_steer_limit_active, ekf.grip_lost, steering, cmd.steering, ekf.yaw_rate, ekf.slip_deg` | вручную: на скользком отношение `steering` к `cmd.steering` и `yaw_rate` | `test_kids_anti_spin.py::test_anti_spin_reduces_slide_on_slippery_turn`, `::test_no_false_slip_on_asphalt` | [ ] |
| 7.6 | Калибровка rx/ry CoM по кругу (TP Ф2): web UI «калибровка CoM», CW и CCW, площадка ≥ 3×3 м. «Длительность круга» 15 с (по умолчанию): сбор начинается, когда установилась скорость (ω), и длится 15 с; на 5 с шум дороги даёт разброс rx/ry ±0.03…0.06 м (SIL, LOS-380). **Пульт выключен** весь прогон (при активном RC калибровка сразу `failed`). Повторная калибровка уточняет сохранённый offset: `get_com_offset_status` показывает итоговый rx/ry (он же в строке `Saved com_offset: …` в `make monitor`) | калибровка `valid`; \|rx\|, \|ry\| ≤ 0.3 м; после неё остаточное смещение по кругам CW и CCW ≤ 0.05 м (slip не критерий: на круге он есть и без смещения, в SIL после коррекции бывает и больше) | PSRAM на каждую калибровку целиком (оба круга): первая — «до» (едет со старым offset), повторная — «после»; `get_com_offset_status` | `trial_metrics.py after.csv --only com-offset --baseline before.csv`: rx/ry калибровок из событий `ComCalibDone` (без событий — FAIL: нужна выгрузка из web UI), остаточное смещение ± погрешность «до» и «после» (по установившимся кругам, `ax`/`ay` в логе уже с поправкой), медиана \|slip\| для справки | `test_com_offset.py::test_calibration_recovers_imu_offset` (±0.04 м), `::test_recalibration_refines_saved_offset`; GTest `test_com_offset_calibration.cpp`; скрипт — `test_trial_metrics.py::test_com_residual_before_and_after_calibration` | [ ] rx ___ ry ___ |
| 7.7 | Madgwick β = 0.05 / 0.1 / 0.2 (TP Ф3), поле «Madgwick β» в web UI, затем «Сохранить»; на время прогонов `adaptive_beta_enabled` = false (иначе на манёвре β обнуляется), после — вернуть | минимальный β, при котором pitch/roll успевают за манёвром (нет запаздывания на склоне и в повороте) | PSRAM на каждый β: заезд на склон и поворот на ровном, после каждого — остановка и покой ≥ 3 с | `trial_metrics.py b005.csv --only madgwick-beta --baseline b01.csv`: в покое — насколько pitch/roll в начале покоя дальше от установившихся и за сколько сходятся до 1° (запаздывание; если ещё ползут в конце покоя — «не сошёлся», нужен покой дольше), в повороте — крен Madgwick p95 (ложный наклон); β и `adaptive_beta_enabled` берутся из снапшотов конфигурации в логе | `test_filter_tuning.py::test_low_beta_lags_on_slope`, `::test_high_beta_tilts_on_maneuver` (только направление компромисса); скрипт — `test_trial_metrics.py::test_tilt_errors_show_beta_tradeoff` | [ ] β = ___ |
| 7.8 | LPF cutoff, грунт против асфальта (TP Ф3), default 30 Гц, поле «LPF gz, Гц» в web UI (5–100), затем «Сохранить». Перед прогонами вернуть режим Normal и стабилизацию (`enabled` = true): после 7.5a они остались Kids и выключены, а yaw-step меряет PID Normal | вибрация подвески в `yaw_rate_dps` (gz после LPF) подавлена, шаг yaw rate (7.1) не хуже | PSRAM, одинаковый проезд на каждом cutoff, **управление с пульта** (иначе нет `rc_steering` для yaw-step) | `trial_metrics.py c10.csv --only lpf --baseline c30.csv`: мощность `yaw_rate_dps` выше 10 Гц относительно yaw rate до LPF (восстановлен из `gx/gy/gz`, монтаж IMU не мешает), дБ (Уэлч; лог 100 Гц — спектр до 50 Гц), падает с cutoff; шаг yaw rate против эталона 30 Гц по худшему шагу — FAIL, если overshoot хуже больше чем на 5 п.п. или settling на 20 мс; по шагам — `--only yaw-step` | `test_filter_tuning.py::test_lower_cutoff_suppresses_vibration_and_lags` (только направление компромисса); скрипт — `test_trial_metrics.py::test_lpf_attenuation_grows_as_cutoff_drops` | [ ] ___ Гц |
| 7.9 | EKF против мерной ленты 5–10 м (TP Ф3) | дистанция по `speed_ms` в пределах ±5 % | PSRAM; старт и финиш из покоя, участок `--from-ms/--to-ms` — от роста `speed_ms` до возврата к 0 | `trial_metrics.py run.csv --only ekf-dist --distance-m 5.0 --from-ms <t0> --to-ms <t1>` (t0, t1 — `ts_ms` начала и конца участка) | `test_trial_metrics.py::test_ekf_distance_error` | [ ] ___ % |

Oversteer guard в Normal **по умолчанию выключен** (`warn_enabled=false`,
`throttle_reduction=0`). Перед 7.3 включить его в web UI и задать reduction,
например 0.7 как в Kids, иначе тест ничего не проверит. Пороги Normal
(slip 20°, rate 50 °/с) выше, чем в SIL-тесте (5°, 10 °/с), а EKF занижает slip
в заносе — снизить их, иначе `oversteer_active` может не включиться вовсе. Пункт IC D1#15 до этого
проходит тривиально. Pitch comp перед 7.3 **выключить** (после 7.2 она могла остаться
включённой): она тоже снижает `cmd_throttle`, а в логе её вклад не отделить от guard, и
прогон с reduction = 0 ложно пройдёт по газу.

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

- Kids anti-spin: признак пробуксовки сделан без датчика (LOS-385); после
  датчика Холла сравнивать скорость колеса с корпусом напрямую.
- Датчик Холла (LOS-238).
