"""Replay записанной телеметрии через sim_host (FW-S2.2, open-loop).

Загружает `telemetry_log_*.csv` (формат прошивки) → кадры протокола sim_host,
прогоняет batch-режимом и проверяет инварианты. Фиделити «со средней точки»:
лог хранит уже калиброванный IMU → sim_host запускается с --identity-calib,
данные текут через Madgwick/EKF/control как есть.
"""

import csv
import math

from .frame import SensorFrame


def _f(row: dict, key: str, default: float = 0.0) -> float:
    v = row.get(key, "")
    if v is None or v == "":
        return default
    try:
        return float(v)
    except ValueError:
        return default


def load_telemetry_csv(path: str) -> list[SensorFrame]:
    """Прочитать CSV прошивки → список кадров для sim_host.

    Используются: `ts_ms` (→ dt), калиброванный IMU `ax..gz`, сырой mag
    `mx/my/mz`, команды `rc_throttle/rc_steering`. Разбор по именам колонок
    (csv.DictReader) — устойчив к порядку/доп. полям.
    """
    frames: list[SensorFrame] = []
    with open(path, newline="") as fh:
        reader = csv.DictReader(fh)
        prev_ts: float | None = None
        for row in reader:
            ts = _f(row, "ts_ms")
            if prev_ts is None:
                dt_ms = 2
            else:
                dt_ms = max(1, int(round(ts - prev_ts)))
            prev_ts = ts
            frames.append(SensorFrame(
                dt_ms=dt_ms,
                ax=_f(row, "ax"), ay=_f(row, "ay"), az=_f(row, "az"),
                gx=_f(row, "gx"), gy=_f(row, "gy"), gz=_f(row, "gz"),
                mag_present=True,
                mx=_f(row, "mx"), my=_f(row, "my"), mz=_f(row, "mz"),
                rc_present=True,
                rc_throttle=_f(row, "rc_throttle"),
                rc_steering=_f(row, "rc_steering"),
            ))
    return frames


# Порог «расхождения» дисперсий EKF — на порядки выше нормальных значений.
_VAR_DIVERGE = 1.0e6


def find_invariant_violations(rows: list[dict]) -> list[str]:
    """Проверить инварианты на выходе sim_host. Пустой список = всё чисто.

    - нет NaN/inf ни в одном поле;
    - applied throttle/steering ∈ [-1, 1];
    - дисперсии EKF (`ekf_*_var`) не расходятся.
    """
    bad: list[str] = []
    for i, r in enumerate(rows):
        for k, v in r.items():
            if not math.isfinite(v):
                bad.append(f"row {i}: {k} non-finite ({v})")
        if not -1.0001 <= r.get("throttle", 0.0) <= 1.0001:
            bad.append(f"row {i}: throttle out of range ({r['throttle']})")
        if not -1.0001 <= r.get("steering", 0.0) <= 1.0001:
            bad.append(f"row {i}: steering out of range ({r['steering']})")
        for vk in ("ekf_vx_var", "ekf_vy_var", "ekf_r_var"):
            if r.get(vk, 0.0) > _VAR_DIVERGE:
                bad.append(f"row {i}: {vk} diverged ({r[vk]})")
    return bad
