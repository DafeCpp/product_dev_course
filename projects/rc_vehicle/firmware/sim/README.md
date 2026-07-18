# rc-sim — физ-модель машинки для SIL-симуляции (FW-S2.4)

Часть эпика [FW-S2](../tasks/FW-S2-sil-physics-model-epic.md). Чистый Python
(numpy): модель динамики RC-машины + синтез **сырых** сенсоров в единицах
прошивки (accel g, gyro dps, mag мГс).

## Состав

- `simlib/sim_params.py` — `SimParams`: масса, база, ЦМ, жёсткость увода, мотор
  (τ, breakaway), серво (лимит, slew), ориентация IMU. Подгонка под реальные
  логи — FW-S2.6, готовые наборы параметров — FW-S2.9 (профили).
- `simlib/profiles.py` + `simlib/data/profiles/*.json` — именованные профили
  параметров (FW-S2.9).
- `simlib/vehicle_model.py` — `VehicleModel`: кинематический велосипед
  (`ψ̇ = v·tan(δ)/L`) + опциональный динамический (боковой увод Caf/Car); мотор
  первого порядка, серво со slew.
- `simlib/sensors.py` — синтез accel/gyro/mag из состояния модели.
- `simlib/frame.py` — `SensorFrame.to_csv()`: формат кадра **совпадает** с
  протоколом `sim_host` (FW-S2.1, `ParseInputLine`). Бинд к exe — в FW-S2.5.

## Replay записанной телеметрии (FW-S2.2)

- `simlib/replay.py` — `load_telemetry_csv()` (формат прошивки → кадры) +
  `find_invariant_violations()` (нет NaN, throttle/steering ∈ [-1,1], EKF не
  расходится).
- `simlib/sim_host_runner.py` — `find_sim_host()` + `run_batch()`: прогон через
  `sim_host` (FW-S2.1) в batch-режиме, фиделити «со средней точки» (`--identity-calib`).
- `tests/fixtures/rides/` — golden-фикстуры (+ `PROVENANCE.md`, генератор синтетики).

Для replay-теста нужен собранный `sim_host`:
```bash
cd projects/rc_vehicle/firmware/tests && cmake -B build && cmake --build build
# путь можно переопределить: export SIM_HOST_BIN=/path/to/sim_host
```
Если бинарь не найден — replay-тест помечается skip (загрузчик/инварианты тестируются всё равно).

## Closed-loop SIL (FW-S2.5)

`simlib/closed_loop.py` — `ClosedLoopSim`: замыкает Python-модель ↔ `sim_host`
(interactive). На тик: сценарий задаёт RC-команду → прошивка считает applied PWM
→ модель интегрирует шаг → синтезирует сырые сенсоры → обратно. Детерминированно,
полный тракт прошивки в контуре.

```python
from simlib import ClosedLoopSim, find_sim_host
with ClosedLoopSim(find_sim_host()) as sim:
    rows = sim.run(600, rc_throttle=0.4, rc_steering=0.5)  # step-руль
    print(sim.model.state.psi)  # машина повернула
```
Сценарии в `tests/test_closed_loop.py`: рамп газа, step-руль (симметрия),
failsafe, **регресс FW-R17** (нет фантомного руля с буста), задний ход (FW-R22),
детерминизм, финитность/диапазон.

## Валидация модели на реальных логах (FW-S2.6)

`simlib/validation.py` — прогон модели записанными командами лога и сравнение с
записанными сенсорами (yaw rate, скорость, продольное ускорение): метрики
RMSE/корреляция на канал + подгонка `SimParams` (scipy, Nelder-Mead). Прошивка
не участвует — чистая модель против записи.

```bash
python validate_logs.py path/to/telemetry_log.csv            # подгонка + метрики
python validate_logs.py path/to/telemetry_log.csv --plot out.png --dynamic
```
Тесты (`tests/test_validation.py`) — round-trip: «запись» из модели с известными
параметрами → подгонка восстанавливает их (детерминированно, без реальных логов).
Реальный лог можно прогнать через `SIM_VALIDATION_LOG=path pytest -k real_log`.

## Профили параметров (FW-S2.9)

`simlib/profiles.py` — именованные наборы `SimParams`. Источник истины — JSON в
`simlib/data/profiles/`; у каждого профиля есть категория происхождения
(`measured` / `baseline` / `synthetic`), см. `data/profiles/PROVENANCE.md`.

```bash
python validate_logs.py --list-profiles
python validate_logs.py log.csv --profile heavy --no-fit    # оценить профиль как есть
python validate_logs.py log.csv --profile drift             # фит со старта профиля
python validate_logs.py log.csv --save-profile my.json      # подогнать под своё шасси
```
```python
from simlib import ClosedLoopSim, find_sim_host, get_profile
with ClosedLoopSim(find_sim_host(), params=get_profile("heavy")) as sim:
    sim.run(600, rc_throttle=0.4)
```
`--profile` принимает имя встроенного профиля **или** путь к JSON; при коллизии
имён выигрывает встроенный. Загрузка допускает частичный `params` (недостающее —
из дефолтов), `save_profile` всегда пишет полный дамп.

Измерен только `fitted_2026_07_18` (FW-S2.6); `light`/`heavy`/`drift` — расчётные
из него, ни одно шасси не взвешивалось. `light` — **не** «детский режим»: лёгкая
машина с тем же мотором разгоняется резче; лимиты для детей живут в прошивке
(`KidsMode`). `drift` требует `dynamic=True` (иначе `Caf`/`Car` не участвуют) и
осмысленен ниже критической скорости 6.0 м/с — выше линейная модель шин расходится.

**Дефолты не изменились:** `VehicleModel()`/`ClosedLoopSim()` без явных `params`
по-прежнему берут `SimParams()`. Переключение дефолта на измеренный профиль —
отдельная задача (LOS-224).

## Запуск тестов

```bash
cd projects/rc_vehicle/firmware/sim
python3 -m venv .venv && . .venv/bin/activate
pip install -e ".[dev]"
python -m pytest
```

## Контракт кадра (17 полей CSV)

```
dt_ms, ax,ay,az, gx,gy,gz, mag_present,mx,my,mz,
rc_present,rc_thr,rc_str, wifi_present,wifi_thr,wifi_str
```
Единицы: accel g, gyro dps, mag мГс — как `ImuData`/`MagData` в прошивке.
