# rc-sim — физ-модель машинки для SIL-симуляции (FW-S2.4)

Часть эпика [FW-S2](../tasks/FW-S2-sil-physics-model-epic.md). Чистый Python
(numpy): модель динамики RC-машины + синтез **сырых** сенсоров в единицах
прошивки (accel g, gyro dps, mag мГс).

## Состав

- `simlib/sim_params.py` — `SimParams`: масса, база, ЦМ, жёсткость увода, мотор
  (τ, breakaway), серво (лимит, slew), ориентация IMU. Подгонка под реальные
  логи — в FW-S2.6.
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
