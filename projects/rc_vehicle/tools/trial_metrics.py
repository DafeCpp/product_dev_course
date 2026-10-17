#!/usr/bin/env python3
"""Метрики испытаний по логу заезда (LOS-363, TESTING_PROGRAM.md).

На сессии с железом достаточно записать лог (PSRAM 100 Гц или UDP), метрики
считаются автоматически и сравниваются с критериями программы испытаний.
Те же функции гоняются на логах SIL (`sim/tests/test_trial_metrics.py`), так что
скрипт отлаживается без машины.

Формат входа — CSV `TelemetryLogFrame` (как у `analyze_telemetry.py`).

    python3 trial_metrics.py log.csv                       # всё, что можно посчитать
    python3 trial_metrics.py log.csv --only yaw-step failsafe
    python3 trial_metrics.py log.csv --distance-m 5.0      # + EKF против мерной ленты

Метрики:
  yaw-step    overshoot и settling time yaw rate после шага руля (только RC, Normal)
  gyro-bias   дрейф bias гироскопа и pitch/roll в покое
  failsafe    время от пропадания источников до нейтрали (≤250 мс)
  kids        максимальные скорость и ускорение в Kids Mode
  ekf-dist    интеграл EKF speed_ms против известной дистанции (нужен --distance-m)
"""

from __future__ import annotations

import argparse
import math
import os
import sys
from dataclasses import dataclass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from analyze_telemetry import (  # noqa: E402
    Row,
    col_raw,
    detect_stationary_mask,
    has_col,
    load_csv,
)

G = 9.80665

# Целевые значения из TESTING_PROGRAM.md.
YAW_OVERSHOOT_LIMIT = 0.20
YAW_SETTLING_LIMIT_MS = 200.0
YAW_SETTLING_BAND = 0.10
FAILSAFE_LIMIT_MS = 250.0
# Normal mode: уставка yaw rate = руль × 90 °/с (common/drive_modes.cpp).
NORMAL_STEER_TO_YAW_DPS = 90.0
# Таймауты источников до того, как rc_ok/wifi_ok упадут (common/config.hpp).
RC_SOURCE_TIMEOUT_MS = 90.0
WIFI_SOURCE_TIMEOUT_MS = 100.0
NEUTRAL_EPS = 1e-3
# Grace-период Failsafe (FailsafeConfig::kTimeoutMs): провал флагов короче —
# не потеря связи, failsafe в прошивке и не должен сработать.
FAILSAFE_GRACE_MS = 120.0
# Шаг руля считается, только если после него команда не меньше этого (не возврат к нулю).
STEP_MIN_TARGET = 0.1
# Покой короче этого не годится для тренда bias (программа: 5-10 мин).
MIN_REST_S = 60.0
DRIVE_MODE_KIDS = 3  # DRIVE_MODE_NAMES в analyze_telemetry.py


@dataclass(frozen=True)
class MetricResult:
    """`passed=None` — критерия нет, только измерение."""

    name: str
    passed: bool | None
    value: str
    # В логе нет данных для метрики (нет шага руля, покоя, потери связи...).
    no_data: bool = False


def _ts(rows: list[Row]) -> list[float]:
    return col_raw(rows, "ts_ms")


def _finite(values: list[float]) -> list[float]:
    return [v for v in values if not math.isnan(v)]


# ---------------------------------------------------------------------------
# Yaw PID: overshoot и settling time
# ---------------------------------------------------------------------------

def find_steering_steps(rows: list[Row], min_step: float = 0.2,
                        settle_ms: float = 100.0) -> list[int]:
    """Индексы шагов команды руля: |Δ| ≥ min_step за `settle_ms`.

    Берётся команда водителя `rc_steering`, а не `cmd_steering`/`steering`: те
    уже содержат коррекцию стабилизации. Прошивка пишет `rc_steering` только
    пока активен приёмник RC (`telemetry_builder.cpp`), поэтому при управлении
    по Wi-Fi шагов в логе нет. Возврат руля к нулю — не шаг: уставки нет.
    """
    if not has_col(rows, "rc_steering"):
        return []
    ts, cmd = _ts(rows), col_raw(rows, "rc_steering")
    steps: list[int] = []
    j = 0
    for i in range(1, len(rows)):
        while ts[i] - ts[j] > settle_ms:
            j += 1
        if abs(cmd[i] - cmd[j]) >= min_step and abs(cmd[i]) >= STEP_MIN_TARGET and (
                not steps or ts[i] - ts[steps[-1]] > 2 * settle_ms):
            # Начало шага — последний отсчёт до первого изменения команды.
            k = j
            while k < i and abs(cmd[k + 1] - cmd[j]) < 1e-3:
                k += 1
            steps.append(k)
    return steps


def _clip_at_release(cmd: list[float], start: int, end: int,
                     release: float = 0.2) -> int:
    """Последний отсчёт окна до того, как водитель отпустил руль.

    Отпусканием считается откат команды от достигнутого пика на `release` и
    больше (возврат к нулю или встречный поворот): дальше yaw rate описывает
    уже другой манёвр.
    """
    seg = cmd[start:end + 1]
    peak_at = max(range(len(seg)), key=lambda k: abs(seg[k]))
    sign = math.copysign(1.0, seg[peak_at]) if seg[peak_at] else 1.0
    peak = -math.inf
    for k, v in enumerate(seg):
        if math.isnan(v):
            continue
        peak = max(peak, sign * v)
        if peak - sign * v >= release:
            return start + max(k - 1, 0)
    return end


def yaw_step_response(rows: list[Row], start: int, window_ms: float = 1000.0,
                      gain_dps: float = NORMAL_STEER_TO_YAW_DPS) -> dict:
    """Overshoot и settling yaw rate после шага руля, начавшегося в `start`.

    Уставка = (руль после шага) × `gain_dps`. Overshoot — пик yaw rate над
    уставкой в долях уставки; settling — момент, после которого yaw rate
    остаётся в полосе ±10 % от уставки до конца окна.
    """
    ts, cmd = _ts(rows), col_raw(rows, "rc_steering")
    yaw = col_raw(rows, "yaw_rate_dps")
    t0 = ts[start]
    end = start
    while end < len(rows) - 1 and ts[end + 1] - t0 <= window_ms:
        end += 1
    end = _clip_at_release(cmd, start, end)
    # Руль после шага: медиана последней четверти окна (команда уже установилась).
    tail = sorted(cmd[start + (end - start) * 3 // 4: end + 1])
    steer = tail[len(tail) // 2]
    sign = math.copysign(1.0, steer) if steer else 1.0
    setpoint = abs(steer) * gain_dps
    if setpoint < 1e-6:
        raise ValueError("шаг руля без ненулевой уставки")
    y = [sign * v for v in yaw[start:end + 1]]
    t = [v - t0 for v in ts[start:end + 1]]
    overshoot = (max(y) - setpoint) / setpoint
    out = [k for k, v in enumerate(y)
           if abs(v - setpoint) > YAW_SETTLING_BAND * setpoint]
    if not out:
        settling = 0.0
    elif out[-1] == len(y) - 1:
        settling = math.inf  # к концу окна так и не вошёл в полосу
    else:
        settling = t[out[-1] + 1]
    return {"setpoint_dps": setpoint, "overshoot": overshoot,
            "settling_ms": settling, "t0_ms": t0}


def check_yaw_step(rows: list[Row], gain_dps: float | None = None
                   ) -> list[MetricResult]:
    """Overshoot/settling для каждого шага руля из `rc_steering`.

    `gain_dps=None` — уставка Normal-режима (90 °/с на единицу руля); в логе с
    колонкой `drive_mode` шаги в других режимах пропускаются: их коэффициент
    другой, его задают явно через `--gain-dps`.
    """
    name = "Yaw PID step"
    if not has_col(rows, "yaw_rate_dps"):
        return [MetricResult(name, False, "НЕТ КОЛОНКИ yaw_rate_dps", no_data=True)]
    steps = find_steering_steps(rows)
    if not steps:
        return [MetricResult(
            name, False,
            "ШАГ РУЛЯ НЕ НАЙДЕН (в лог пишется только команда с пульта RC; при "
            "управлении по Wi-Fi её нет)", no_data=True)]
    ts = _ts(rows)
    mode = col_raw(rows, "drive_mode") if has_col(rows, "drive_mode") else None
    out = []
    for n, start in enumerate(steps, 1):
        label = f"{name} #{n} (t={ts[start]:.0f} мс)"
        if gain_dps is None and mode is not None and mode[start + 1] != 0:
            out.append(MetricResult(
                label, None,
                f"режим {mode[start + 1]:.0f} не Normal: уставка не 90 °/с, "
                "задайте --gain-dps", no_data=True))
            continue
        gain = NORMAL_STEER_TO_YAW_DPS if gain_dps is None else gain_dps
        # Окно кончается на следующем шаге, чтобы не мерить чужой переходный процесс.
        window = 1000.0
        if n < len(steps):
            window = min(window, ts[steps[n]] - ts[start])
        try:
            r = yaw_step_response(rows, start, window_ms=window, gain_dps=gain)
        except ValueError as exc:
            out.append(MetricResult(label, None, str(exc), no_data=True))
            continue
        ok_over = r["overshoot"] < YAW_OVERSHOOT_LIMIT
        ok_settle = r["settling_ms"] < YAW_SETTLING_LIMIT_MS
        out.append(MetricResult(
            label, ok_over and ok_settle,
            f"уставка {r['setpoint_dps']:.1f} °/с, overshoot "
            f"{r['overshoot'] * 100:.1f} % (< {YAW_OVERSHOOT_LIMIT * 100:.0f} %"
            f"{'' if ok_over else ', НЕ ВЫПОЛНЕНО'}), settling "
            f"{r['settling_ms']:.0f} мс (< {YAW_SETTLING_LIMIT_MS:.0f} мс"
            f"{'' if ok_settle else ', НЕ ВЫПОЛНЕНО'})"))
    return out


# ---------------------------------------------------------------------------
# Дрейф bias гироскопа и ориентации в покое
# ---------------------------------------------------------------------------

def _slope_per_min(ts_ms: list[float], values: list[float]) -> float:
    """Наклон линейной аппроксимации, единиц в минуту."""
    n = len(values)
    if n < 3:
        return math.nan
    tm = sum(ts_ms) / n
    vm = sum(values) / n
    den = sum((t - tm) ** 2 for t in ts_ms)
    if den == 0:
        return math.nan
    num = sum((t - tm) * (v - vm) for t, v in zip(ts_ms, values))
    return num / den * 60_000.0


def stationary_drift(rows: list[Row], mask: list[bool] | None = None) -> dict:
    """Дрейф gx/gy/gz (°/с в минуту), pitch/roll (° в минуту) в покое.

    Берутся только неподвижные отсчёты (`detect_stationary_mask`). `bias_*` —
    среднее; `span_*` — разность средних последней и первой пятой частей
    покоя, то есть уход bias за запись.
    """
    if mask is None:
        mask = detect_stationary_mask(rows)
    idx = [i for i, m in enumerate(mask) if m]
    if len(idx) < 10:
        raise ValueError("мало неподвижных отсчётов")
    ts = _ts(rows)
    t = [ts[i] for i in idx]
    # Длительность — сумма интервалов внутри непрерывных кусков покоя, а не
    # размах первого и последнего отсчёта (между кусками может быть езда).
    rest_ms = sum(ts[b] - ts[a] for a, b in zip(idx, idx[1:]) if b == a + 1)
    out: dict = {"duration_s": rest_ms / 1000.0, "samples": len(idx)}
    for ch in ("gx", "gy", "gz", "pitch_deg", "roll_deg"):
        if not has_col(rows, ch):
            continue
        raw = col_raw(rows, ch)
        v = [raw[i] for i in idx]
        pairs = [(a, b) for a, b in zip(t, v) if not math.isnan(b)]
        if len(pairs) < 10:
            continue
        tt, vv = [p[0] for p in pairs], [p[1] for p in pairs]
        k = max(1, len(vv) // 5)
        out[ch] = {
            "mean": sum(vv) / len(vv),
            "slope_per_min": _slope_per_min(tt, vv),
            "span": sum(vv[-k:]) / k - sum(vv[:k]) / k,
        }
    return out


def check_gyro_bias(rows: list[Row], max_gyro_slope_dps_min: float = 0.5,
                    max_angle_slope_deg_min: float = 1.0,
                    min_rest_s: float = MIN_REST_S) -> list[MetricResult]:
    name = "Дрейф в покое"
    try:
        d = stationary_drift(rows)
    except ValueError as exc:
        return [MetricResult(name, False, str(exc).upper(), no_data=True)]
    if d["duration_s"] < min_rest_s:
        return [MetricResult(
            name, False,
            f"покоя {d['duration_s']:.0f} с, нужно ≥ {min_rest_s:.0f} с "
            "(по программе 5-10 мин): тренд за секунды — это шум", no_data=True)]
    out = []
    for ch in ("gx", "gy", "gz"):
        if ch in d:
            s = d[ch]
            out.append(MetricResult(
                f"{name}: bias {ch}",
                abs(s["slope_per_min"]) < max_gyro_slope_dps_min,
                f"среднее {s['mean']:+.4f} °/с, тренд {s['slope_per_min']:+.4f} °/с/мин "
                f"(< {max_gyro_slope_dps_min}), уход за запись {s['span']:+.4f} °/с, "
                f"{d['duration_s']:.0f} с покоя"))
    for ch in ("pitch_deg", "roll_deg"):
        if ch in d:
            s = d[ch]
            out.append(MetricResult(
                f"{name}: {ch}",
                abs(s["slope_per_min"]) < max_angle_slope_deg_min,
                f"тренд {s['slope_per_min']:+.3f} °/мин (< {max_angle_slope_deg_min}), "
                f"уход за запись {s['span']:+.3f} °"))
    if not out:
        out.append(MetricResult(name, False, "НЕТ КОЛОНОК gx/gy/gz", no_data=True))
    return out


# ---------------------------------------------------------------------------
# Failsafe: время до нейтрали
# ---------------------------------------------------------------------------

def failsafe_latencies(rows: list[Row]) -> list[dict]:
    """События пропадания источников и время до нейтрали.

    Событие — переход «хотя бы один источник жив» → «оба молчат»
    (`rc_ok`/`wifi_ok`). Нейтраль — первый отсчёт после него, где применённые
    газ и руль равны нулю. Флаги падают уже после таймаута самого источника
    (RC 90 мс, Wi-Fi 100 мс), поэтому полное время = от флага + таймаут источника.
    Это нижняя оценка: интервал опроса RC (20 мс) и ширина импульса не учтены
    (в прошивке на них заложен запас `kLossToNeutralMs`). Нейтраль ищется до
    возврата источника.
    """
    ts = _ts(rows)
    rc, wifi = col_raw(rows, "rc_ok"), col_raw(rows, "wifi_ok")
    thr, st = col_raw(rows, "throttle"), col_raw(rows, "steering")
    # Источник, который был жив до потери: от него зависит таймаут.
    events = []
    for i in range(1, len(rows)):
        was = rc[i - 1] > 0.5 or wifi[i - 1] > 0.5
        now = rc[i] > 0.5 or wifi[i] > 0.5
        if was and not now:
            # Оба источника были живы: консервативно берём больший таймаут.
            source_timeout = max(
                RC_SOURCE_TIMEOUT_MS if rc[i - 1] > 0.5 else 0.0,
                WIFI_SOURCE_TIMEOUT_MS if wifi[i - 1] > 0.5 else 0.0)
            back_at = next((k for k in range(i, len(rows))
                            if rc[k] > 0.5 or wifi[k] > 0.5), None)
            if back_at is not None and ts[back_at] - ts[i] < FAILSAFE_GRACE_MS:
                continue  # кратковременный провал флагов
            moving = abs(thr[i - 1]) > NEUTRAL_EPS or abs(st[i - 1]) > NEUTRAL_EPS
            neutral_at = None
            for k in range(i, len(rows)):
                if rc[k] > 0.5 or wifi[k] > 0.5:
                    break  # источник вернулся раньше нейтрали
                if abs(thr[k]) < NEUTRAL_EPS and abs(st[k]) < NEUTRAL_EPS:
                    neutral_at = ts[k]
                    break
            events.append({
                "t_ms": ts[i],
                "from_flag_ms": None if neutral_at is None else neutral_at - ts[i],
                "source_timeout_ms": source_timeout,
                "was_moving": moving,
            })
    return events


def check_failsafe(rows: list[Row]) -> list[MetricResult]:
    name = "Failsafe"
    for c in ("rc_ok", "wifi_ok", "throttle", "steering"):
        if not has_col(rows, c):
            return [MetricResult(name, False, f"НЕТ КОЛОНКИ {c}", no_data=True)]
    events = failsafe_latencies(rows)
    if not events:
        return [MetricResult(name, False, "ПРОПАДАНИЕ ИСТОЧНИКОВ НЕ НАЙДЕНО", no_data=True)]
    out = []
    for n, e in enumerate(events, 1):
        label = f"{name} #{n} (t={e['t_ms']:.0f} мс)"
        if e["from_flag_ms"] is None:
            out.append(MetricResult(label, False, "нейтраль не наступила"))
            continue
        total = e["from_flag_ms"] + e["source_timeout_ms"]
        note = "" if e["was_moving"] else " [машина уже стояла: оценка завышена]"
        out.append(MetricResult(
            label, total <= FAILSAFE_LIMIT_MS,
            f"от падения флага {e['from_flag_ms']:.0f} мс + таймаут источника "
            f"{e['source_timeout_ms']:.0f} мс = {total:.0f} мс "
            f"(≤ {FAILSAFE_LIMIT_MS:.0f} мс){note}"))
    return out


# ---------------------------------------------------------------------------
# Kids Mode: максимальные скорость и ускорение
# ---------------------------------------------------------------------------

def kids_peaks(rows: list[Row]) -> dict:
    """Максимум |speed_ms| и |ax| (м/с²) в отсчётах Kids Mode.

    Kids-отсчёты — `drive_mode == 3`. Ускорение — `ax` из лога (g → м/с²): это
    не `forward_accel` прошивки (его в логе нет), значение включает проекцию g
    при наклоне и вибрацию, то есть завышено.
    """
    for c in ("drive_mode", "speed_ms", "ax"):
        if not has_col(rows, c):
            raise ValueError(f"нет колонки {c}")
    mode = col_raw(rows, "drive_mode")
    sel = [i for i, m in enumerate(mode) if m == DRIVE_MODE_KIDS]
    if not sel:
        raise ValueError("нет отсчётов Kids Mode")
    speed = col_raw(rows, "speed_ms")
    ax = col_raw(rows, "ax")
    sp = _finite([abs(speed[i]) for i in sel])
    ac = _finite([abs(ax[i]) * G for i in sel])
    return {"samples": len(sel),
            "max_speed_ms": max(sp) if sp else math.nan,
            "max_accel_ms2": max(ac) if ac else math.nan}


def check_kids(rows: list[Row], max_speed_ms: float | None = None,
               max_accel_ms2: float | None = None) -> list[MetricResult]:
    name = "Kids Mode"
    try:
        p = kids_peaks(rows)
    except ValueError as exc:
        return [MetricResult(name, False, str(exc), no_data=True)]
    ok_speed = None if max_speed_ms is None else p["max_speed_ms"] <= max_speed_ms
    ok_accel = None if max_accel_ms2 is None else p["max_accel_ms2"] <= max_accel_ms2
    lim = lambda v, u: "" if v is None else f" (≤ {v} {u})"  # noqa: E731
    return [
        MetricResult(f"{name}: макс. скорость", ok_speed,
                     f"{p['max_speed_ms']:.2f} м/с{lim(max_speed_ms, 'м/с')}"),
        MetricResult(f"{name}: макс. ускорение", ok_accel,
                     f"{p['max_accel_ms2']:.2f} м/с²{lim(max_accel_ms2, 'м/с²')}"),
    ]


# ---------------------------------------------------------------------------
# EKF: скорость против известной дистанции
# ---------------------------------------------------------------------------

def integrate_distance(rows: list[Row], t_from_ms: float | None = None,
                       t_to_ms: float | None = None) -> float:
    """Путь по EKF `speed_ms` (метод трапеций), метры."""
    ts, speed = _ts(rows), col_raw(rows, "speed_ms")
    total = 0.0
    for i in range(1, len(rows)):
        if math.isnan(speed[i]) or math.isnan(speed[i - 1]):
            continue
        if t_from_ms is not None and ts[i - 1] < t_from_ms:
            continue
        if t_to_ms is not None and ts[i] > t_to_ms:
            break
        dt = max(0.0, ts[i] - ts[i - 1])
        total += 0.5 * (abs(speed[i]) + abs(speed[i - 1])) * dt / 1000.0
    return total


def check_ekf_distance(rows: list[Row], distance_m: float, tolerance: float = 0.05,
                       t_from_ms: float | None = None,
                       t_to_ms: float | None = None) -> list[MetricResult]:
    name = "EKF: дистанция"
    if not has_col(rows, "speed_ms"):
        return [MetricResult(name, False, "НЕТ КОЛОНКИ speed_ms", no_data=True)]
    if distance_m <= 0:
        raise ValueError("distance_m должна быть > 0")
    est = integrate_distance(rows, t_from_ms, t_to_ms)
    err = (est - distance_m) / distance_m
    return [MetricResult(
        name, abs(err) <= tolerance,
        f"EKF {est:.3f} м против {distance_m:.3f} м: ошибка {err * 100:+.1f} % "
        f"(допуск ±{tolerance * 100:.0f} %)")]


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

METRICS = ("yaw-step", "gyro-bias", "failsafe", "kids", "ekf-dist")


def run_metrics(rows: list[Row], only: list[str] | None = None,
                distance_m: float | None = None, **kw) -> list[MetricResult]:
    wanted = set(only or METRICS)
    if distance_m is None:
        wanted.discard("ekf-dist")  # без мерной ленты считать нечего
    out: list[MetricResult] = []
    if "yaw-step" in wanted:
        out += check_yaw_step(rows, kw.get("gain_dps"))
    if "gyro-bias" in wanted:
        out += check_gyro_bias(rows, min_rest_s=kw.get("min_rest_s", MIN_REST_S))
    if "failsafe" in wanted:
        out += check_failsafe(rows)
    if "kids" in wanted:
        out += check_kids(rows, kw.get("kids_max_speed"), kw.get("kids_max_accel"))
    if "ekf-dist" in wanted and distance_m is not None:
        out += check_ekf_distance(rows, distance_m, kw.get("tolerance", 0.05),
                                  kw.get("t_from_ms"), kw.get("t_to_ms"))
    if only is None:
        # Лог одного испытания не содержит данных для остальных метрик:
        # без явного --only это пропуск, а не провал.
        out = [MetricResult(r.name, None, r.value, True) if r.no_data else r
               for r in out]
    return out


def print_results(results: list[MetricResult]) -> None:
    for r in results:
        mark = ("SKIP" if r.no_data else "INFO") if r.passed is None else ("PASS" if r.passed else "FAIL")
        print(f"[{mark}] {r.name}: {r.value}")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="Метрики испытаний по CSV-логу заезда (LOS-363).",
        epilog=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv_path")
    ap.add_argument("--only", nargs="+", choices=METRICS,
                    help="посчитать только эти метрики")
    ap.add_argument("--distance-m", type=float,
                    help="известная дистанция заезда (мерная лента) для ekf-dist")
    ap.add_argument("--tolerance", type=float, default=0.05,
                    help="допуск ekf-dist, доля (по умолчанию 0.05)")
    ap.add_argument("--from-ms", type=float, help="начало участка для ekf-dist (ts_ms)")
    ap.add_argument("--to-ms", type=float, help="конец участка для ekf-dist (ts_ms)")
    ap.add_argument("--gain-dps", type=float,
                    help="уставка yaw rate на единицу руля; по умолчанию Normal "
                         "(90 °/с), шаги в других режимах пропускаются")
    ap.add_argument("--min-rest-s", type=float, default=MIN_REST_S,
                    help="минимальная длительность покоя для gyro-bias, с "
                         f"(по умолчанию {MIN_REST_S:.0f})")
    ap.add_argument("--kids-max-speed", type=float, help="лимит скорости Kids, м/с")
    ap.add_argument("--kids-max-accel", type=float, help="лимит ускорения Kids, м/с²")
    return ap.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not os.path.isfile(args.csv_path):
        print(f"ERROR: файл не найден: {args.csv_path}", file=sys.stderr)
        return 1
    if args.distance_m is not None and args.distance_m <= 0:
        print("ERROR: --distance-m должна быть > 0", file=sys.stderr)
        return 1
    _, rows = load_csv(args.csv_path)
    if not rows or "ts_ms" not in rows[0]:
        print("ERROR: пустой лог или нет колонки ts_ms", file=sys.stderr)
        return 1
    results = run_metrics(
        rows, args.only, args.distance_m, gain_dps=args.gain_dps,
        kids_max_speed=args.kids_max_speed, kids_max_accel=args.kids_max_accel,
        tolerance=args.tolerance, t_from_ms=args.from_ms, t_to_ms=args.to_ms,
        min_rest_s=args.min_rest_s)
    print_results(results)
    failed = [r for r in results if r.passed is False]
    checked = [r for r in results if r.passed is not None]
    if failed:
        print(f"RESULT: {len(failed)} из {len(checked)} критериев не выполнены.")
    elif not checked and not any(r.passed is None and not r.no_data for r in results):
        print("RESULT: нет данных ни для одной метрики.")
        return 2
    elif not checked:
        print("RESULT: измерено, критерии не заданы.")
    else:
        print("RESULT: все критерии выполнены.")
    return 2 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
