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
ROAD_CHANNELS = ("ax", "ay", "az", "gx", "gy")


def _corr(a, b) -> float:
    a, b = np.asarray(a, float), np.asarray(b, float)
    if len(a) < 3 or np.std(a) < 1e-9 or np.std(b) < 1e-9:
        return float("nan")
    return float(np.corrcoef(a, b)[0, 1])


def _maybe_fix_legacy_signs(ts, yaw_rate, yaw_deg, speed_ms, long_accel):
    """Поддержать старые LOS-213 логи, не трогая новые логи прошивки.

    Начиная с LOS-222 прошивка должна писать yaw_rate_dps и ax уже в СК
    автомобиля. Старые записанные CSV из LOS-213 остаются полезны для CI и
    ретро-валидации, поэтому детектируем их по физическим связям внутри лога и
    компенсируем только при явно отрицательной корреляции.
    """
    if yaw_deg:
        ts_arr = np.asarray(ts, float)
        yaw_deg_arr = np.asarray(yaw_deg, float)
        dt = np.diff(ts_arr, prepend=ts_arr[0] - 2.0) / 1000.0
        dt = np.clip(dt, 1e-3, 0.1)
        yaw_from_deg = np.diff(yaw_deg_arr, prepend=yaw_deg_arr[0]) / dt
        if _corr(yaw_from_deg, yaw_rate) < -0.5:
            yaw_rate = [-v for v in yaw_rate]

    if len(speed_ms) >= 3:
        ts_arr = np.asarray(ts, float)
        spd_arr = np.asarray(speed_ms, float)
        dt = np.diff(ts_arr, prepend=ts_arr[0] - 2.0) / 1000.0
        dt = np.clip(dt, 1e-3, 0.1)
        accel_from_speed = np.diff(spd_arr, prepend=spd_arr[0]) / dt
        if _corr(accel_from_speed, long_accel) < -0.5:
            long_accel = [-v for v in long_accel]

    return yaw_rate, long_accel


def load_drive_log(path: str, use_applied: bool = True) -> dict:
    """Прочитать лог прошивки → команды (вход модели) + записанные сенсоры.

    `use_applied=True` — кормить модель ПРИМЕНЁННЫМ выходом (`throttle`/`steering`,
    физический вход актуаторов); False — сырыми командами пульта (`rc_*`).
    """
    ts, thr, steer, yaw, yaw_deg, spd, lon = [], [], [], [], [], [], []
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
            if "yaw_deg" in row:
                yaw_deg.append(f("yaw_deg"))
            spd.append(f("speed_ms"))
            lon.append(f("ax") * _G)

    yaw, lon = _maybe_fix_legacy_signs(ts, yaw, yaw_deg, spd, lon)

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


def load_road_noise_log(path: str, window: int = 21) -> dict:
    """Прочитать лог и выделить высокочастотную компоненту IMU (LOS-287).

    Тот же centred moving-average на 21 тик использован при исходном анализе
    заездов. Края окна отбрасываются, чтобы zero-padding не завышал sigma.
    """
    if window < 3 or window % 2 == 0:
        raise ValueError("window должен быть нечётным и >= 3")
    columns = ("ts_ms", "speed_ms", *ROAD_CHANNELS)
    rows = []
    with open(path, newline="") as fh:
        for row in csv.DictReader(fh):
            try:
                rows.append([float(row[key]) for key in columns])
            except (KeyError, TypeError, ValueError):
                continue
    if len(rows) < window:
        raise ValueError(f"{path}: меньше {window} валидных IMU-строк")

    raw = np.asarray(rows, float)
    half = window // 2
    kernel = np.ones(window, float) / window
    residual = np.column_stack([
        raw[:, i] - np.convolve(raw[:, i], kernel, mode="same")
        for i in range(2, len(columns))
    ])[half:-half]
    ts = raw[half:-half, 0]
    speed = np.abs(raw[half:-half, 1])
    positive_dt = np.diff(ts)
    positive_dt = positive_dt[positive_dt > 0.0]
    if len(positive_dt) == 0:
        raise ValueError(f"{path}: нет возрастающих ts_ms")
    dt_s = float(np.median(positive_dt) / 1000.0)
    return {
        "path": path,
        "speed_ms": speed,
        "residual": {name: residual[:, i]
                     for i, name in enumerate(ROAD_CHANNELS)},
        "dt_s": dt_s,
    }


def _sigma_points(logs: list[dict], channel: str, bin_width: float,
                  min_samples: int) -> tuple[np.ndarray, np.ndarray]:
    speeds, sigmas = [], []
    for log in logs:
        v = log["speed_ms"]
        x = log["residual"][channel]
        if len(v) == 0:
            continue
        for lo in np.arange(0.0, float(np.max(v)) + bin_width, bin_width):
            mask = (v >= lo) & (v < lo + bin_width)
            if int(np.count_nonzero(mask)) >= min_samples:
                speeds.append(float(np.mean(v[mask])))
                sigmas.append(float(np.std(x[mask])))
    return np.asarray(speeds), np.asarray(sigmas)


def _fit_nonnegative_line(x: np.ndarray, y: np.ndarray) -> tuple[float, float]:
    if len(x) < 2:
        raise ValueError("недостаточно скоростных корзин для fit sigma(v)")
    from scipy.optimize import nnls

    coeff, _ = nnls(np.column_stack((np.ones_like(x), x)), y)
    return float(coeff[0]), float(coeff[1])


def _fit_resonator(logs: list[dict], channel: str) -> tuple[float, float]:
    """Оценить frequency/damping angle-резонатора по ACF его rate-выхода."""
    observations = []
    for log in logs:
        x = log["residual"][channel]
        mask = log["speed_ms"] >= 0.5
        pair1 = mask[1:] & mask[:-1]
        pair2 = mask[2:] & mask[:-2]
        if np.count_nonzero(pair2) < 100:
            continue
        centered = x - float(np.mean(x[mask]))
        variance = float(np.mean(centered[mask] ** 2))
        if variance < 1.0e-12:
            continue
        r1 = float(np.mean(centered[1:][pair1] * centered[:-1][pair1]) / variance)
        r2 = float(np.mean(centered[2:][pair2] * centered[:-2][pair2]) / variance)
        observations.append((log["dt_s"], r1, r2))
    if not observations:
        raise ValueError(f"не удалось оценить спектр {channel}")

    def predicted_rate_acf(frequency: float, damping: float,
                           dt_s: float) -> tuple[float, float]:
        omega = 2.0 * math.pi * frequency
        radius = math.exp(-damping * omega * dt_s)
        angle = omega * math.sqrt(1.0 - damping * damping) * dt_s
        a1 = 2.0 * radius * math.cos(angle)
        a2 = -(radius * radius)
        rho = [1.0, a1 / (1.0 - a2)]
        for _ in range(2):
            rho.append(a1 * rho[-1] + a2 * rho[-2])
        variance = 2.0 * (1.0 - rho[1])
        return tuple((2.0 * rho[k] - rho[k - 1] - rho[k + 1]) / variance
                     for k in (1, 2))

    from scipy.optimize import least_squares

    def residual(values):
        frequency, damping = values
        return np.asarray([
            predicted - measured
            for dt_s, r1, r2 in observations
            for predicted, measured in zip(
                predicted_rate_acf(frequency, damping, dt_s), (r1, r2))
        ])

    result = least_squares(
        residual, np.asarray([10.0, 0.35]),
        bounds=(np.asarray([1.0, 0.05]), np.asarray([45.0, 0.95])))
    return float(result.x[0]), float(result.x[1])


def fit_road_noise_params(logs: list[dict], base_params: SimParams,
                          bin_width: float = 0.25,
                          min_samples: int = 100) -> tuple[SimParams, dict]:
    """Подогнать sigma(v) и roll/pitch-спектр с равным весом лог/корзина."""
    mapping = {
        "ax": ("road_ax_sigma_0_g", "road_ax_sigma_per_ms"),
        "ay": ("road_ay_sigma_0_g", "road_ay_sigma_per_ms"),
        "az": ("road_az_sigma_0_g", "road_az_sigma_per_ms"),
        "gx": ("road_roll_rate_sigma_0_dps", "road_roll_rate_sigma_per_ms"),
        "gy": ("road_pitch_rate_sigma_0_dps", "road_pitch_rate_sigma_per_ms"),
    }
    fitted_values = {}
    diagnostics = {"channels": {}}
    for channel, fields_ in mapping.items():
        speed, sigma = _sigma_points(logs, channel, bin_width, min_samples)
        intercept, slope = _fit_nonnegative_line(speed, sigma)
        fitted_values[fields_[0]] = intercept
        fitted_values[fields_[1]] = slope
        diagnostics["channels"][channel] = {
            "bins": len(speed), "sigma_0": intercept, "sigma_per_ms": slope,
        }

    roll_frequency, roll_damping = _fit_resonator(logs, "gx")
    pitch_frequency, pitch_damping = _fit_resonator(logs, "gy")
    fitted_values.update({
        "road_roll_frequency_hz": roll_frequency,
        "road_pitch_frequency_hz": pitch_frequency,
        "road_attitude_damping": (roll_damping + pitch_damping) / 2.0,
    })
    diagnostics["spectrum"] = {
        "roll_frequency_hz": roll_frequency,
        "pitch_frequency_hz": pitch_frequency,
        "damping": fitted_values["road_attitude_damping"],
    }
    return replace(base_params, **fitted_values), diagnostics


def format_road_noise_report(diagnostics: dict) -> str:
    lines = ["channel   bins  sigma_0   sigma_per_ms"]
    for channel, values in diagnostics["channels"].items():
        lines.append(
            f"{channel:7s} {values['bins']:4d}  {values['sigma_0']:8.4f}  "
            f"{values['sigma_per_ms']:12.4f}")
    spectrum = diagnostics["spectrum"]
    lines.append(
        "spectrum: roll={:.2f} Hz, pitch={:.2f} Hz, damping={:.3f}".format(
            spectrum["roll_frequency_hz"], spectrum["pitch_frequency_hz"],
            spectrum["damping"]))
    return "\n".join(lines)


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
        # Записанный speed_ms — модуль (EKF √(vx²+vy²)); модель даёт знаковую
        # скорость, на реверсе сравниваем магнитуды.
        spd[i] = abs(o.speed)
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
    return total if math.isfinite(total) else 1.0e9


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


def fit_params_multi(drives: list[dict], base_params: SimParams,
                     free_fields: list[str], channels=CHANNELS,
                     dynamic: bool = False):
    """Один набор `SimParams` под минимум суммарной ошибки по нескольким логам.

    Каждый лог входит в стоимость с равным весом (сумма нормированных RMSE),
    чтобы короткие заезды не тонули в длинных. Возвращает
    (fitted_params, scipy OptimizeResult).
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
        if np.any(vals <= 0.0):
            return 1.0e9
        p = make(xs)
        total = 0.0
        for d in drives:
            pred = simulate(p, d["throttle"], d["steering"], d["dt"], dynamic)
            total += _cost(pred, d["rec"], channels)
        return total

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
