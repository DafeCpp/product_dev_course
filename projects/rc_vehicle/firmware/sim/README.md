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
