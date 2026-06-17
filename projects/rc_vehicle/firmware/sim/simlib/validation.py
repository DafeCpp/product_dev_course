"""Валидация физ-модели на реальных поездках (FW-S2.6, Фаза 4).

Прошивка НЕ участвует: модель (FW-S2.4) прогоняется записанными командами из
`telemetry_log_*.csv` и сравнивается с записанными сенсорами. Метрики на канал
(RMSE/корреляция) + подгонка `SimParams` (scipy). Мост sim↔реальность: обосновывает
доверие к closed-loop (FW-S2.5).
"""

import csv
import math
from dataclasses import replace

import numpy as np

from .sim_params import SimParams
from .vehicle_model import VehicleModel

_G = 9.80665

# Каналы сравнения: yaw rate (dps), скорость (м/с), продольное ускорение (м/с²).
CHANNELS = ("yaw_rate_dps", "speed_ms", "long_accel_ms2")


def load_drive_log(path: str, use_applied: bool = True) -> dict:
    """Прочитать лог прошивки → команды (вход модели) + записанные сенсоры.

    `use_applied=True` — кормить модель ПРИМЕНЁННЫМ выходом (`throttle`/`steering`,
    физический вход актуаторов); False — сырыми командами пульта (`rc_*`).
    """
    ts, thr, steer, yaw, spd, lon = [], [], [], [], [], []
    with open(path, newline="") as fh:
        for row in csv.DictReader(fh):
            def f(key: str) -> float:
                v = row.get(key, "")
                try:
                    return float(v) if v not in (None, "") else 0.0
                except ValueError:
                    return 0.0

            ts.append(f("ts_ms"))
            thr.append(f("throttle") if use_applied else f("rc_throttle"))
            steer.append(f("steering") if use_applied else f("rc_steering"))
            yaw.append(f("yaw_rate_dps"))
            spd.append(f("speed_ms"))
            lon.append(f("ax") * _G)  # ax в g → м/с²

    ts = np.asarray(ts, float)
    dt = np.diff(ts, prepend=ts[0] - 2.0) / 1000.0
    dt = np.clip(dt, 1e-3, 0.1)
    return {
        "dt": dt,
        "throttle": np.asarray(thr, float),
        "steering": np.asarray(steer, float),
        "rec": {
            "yaw_rate_dps": np.asarray(yaw, float),
            "speed_ms": np.asarray(spd, float),
            "long_accel_ms2": np.asarray(lon, float),
        },
    }


def simulate(params: SimParams, throttle, steering, dt,
             dynamic: bool = False) -> dict:
    """Прогнать модель командами → предсказанные каналы (те же, что в rec)."""
    n = len(dt)
    m = VehicleModel(params, dynamic=dynamic)
    yaw = np.empty(n)
    spd = np.empty(n)
    lon = np.empty(n)
    for i in range(n):
        o = m.step(float(dt[i]), float(throttle[i]), float(steering[i]))
        yaw[i] = math.degrees(o.yaw_rate)
        spd[i] = o.speed
        lon[i] = o.long_accel
    return {"yaw_rate_dps": yaw, "speed_ms": spd, "long_accel_ms2": lon}


def rmse(a, b) -> float:
    return float(np.sqrt(np.mean((np.asarray(a) - np.asarray(b)) ** 2)))


def corr(a, b) -> float:
    a, b = np.asarray(a), np.asarray(b)
    if np.std(a) < 1e-9 or np.std(b) < 1e-9:
        return float("nan")
    return float(np.corrcoef(a, b)[0, 1])


def channel_metrics(pred: dict, rec: dict, channels=CHANNELS) -> dict:
    return {ch: {"rmse": rmse(pred[ch], rec[ch]), "corr": corr(pred[ch], rec[ch])}
            for ch in channels}


def _cost(pred: dict, rec: dict, channels) -> float:
    """Сумма нормированных по std RMSE — равный вес каналов."""
    total = 0.0
    for ch in channels:
        sd = float(np.std(rec[ch])) + 1e-6
        total += rmse(pred[ch], rec[ch]) / sd
    return total


def fit_params(drive: dict, base_params: SimParams, free_fields: list[str],
               channels=CHANNELS, dynamic: bool = False):
    """Подобрать `free_fields` под минимум ошибки (Nelder-Mead).

    Возвращает (fitted_params, scipy OptimizeResult).
    """
    from scipy.optimize import minimize

    x0 = np.array([getattr(base_params, f) for f in free_fields], float)
    scale = np.where(np.abs(x0) > 1e-9, np.abs(x0), 1.0)

    def make(xs):
        return replace(base_params,
                       **{f: float(xs[i] * scale[i])
                          for i, f in enumerate(free_fields)})

    def cost(xs):
        vals = xs * scale
        if np.any(vals <= 0.0):  # физ-параметры положительны
            return 1.0e9
        pred = simulate(make(xs), drive["throttle"], drive["steering"],
                        drive["dt"], dynamic)
        return _cost(pred, drive["rec"], channels)

    res = minimize(cost, x0 / scale, method="Nelder-Mead",
                   options={"xatol": 1e-4, "fatol": 1e-5, "maxiter": 4000})
    return make(res.x), res


def format_report(metrics: dict, fitted: SimParams | None = None,
                  free_fields: list[str] | None = None) -> str:
    lines = ["channel          rmse        corr"]
    for ch, m in metrics.items():
        lines.append(f"{ch:16s} {m['rmse']:9.4f}  {m['corr']:6.3f}")
    if fitted is not None and free_fields:
        lines.append("fitted SimParams:")
        for f in free_fields:
            lines.append(f"  {f} = {getattr(fitted, f):.4f}")
    return "\n".join(lines)


def plot_channels(pred: dict, rec: dict, dt, path: str, channels=CHANNELS) -> None:
    """Сохранить график предсказание-vs-запись по каналам (matplotlib, Agg)."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    t = np.cumsum(dt)
    fig, axs = plt.subplots(len(channels), 1, figsize=(8, 2.2 * len(channels)),
                            sharex=True)
    if len(channels) == 1:
        axs = [axs]
    for ax, ch in zip(axs, channels):
        ax.plot(t, rec[ch], label="recorded", lw=1.0)
        ax.plot(t, pred[ch], label="model", lw=1.0)
        ax.set_ylabel(ch)
        ax.legend(fontsize=7)
    axs[-1].set_xlabel("t, s")
    fig.tight_layout()
    fig.savefig(path, dpi=100)
    plt.close(fig)
