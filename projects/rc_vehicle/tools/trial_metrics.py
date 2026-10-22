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
  esc-start   минимальный газ старта по ступеням газа (протокол 4.2)
  pitch-comp  добавка pitch compensation к газу в гору, под гору и на ровном (7.2)
  oversteer   эпизоды oversteer guard: срезан ли газ, пик заноса (7.3)
  rc-events   события RcLost/RcRestored: причины, длительности, парность (2.5)
  imu-errors  ошибки чтения IMU по событиям ImuReadStats: 0 за минуту (5.1)
  com-offset  итог калибровки CoM и остаточное смещение по кругам CW и CCW (7.6)
  madgwick-beta  схождение pitch/roll в покое (запаздывание), крен в повороте (7.7)
  lpf         подавление yaw rate выше 10 Гц и шаг yaw rate против эталона (7.8)
  grip-fit    угол руля и градиент K признака потери сцепления по кругам (7.5f)

--baseline other.csv добавляет лог для сравнения: до калибровки CoM, с другим β,
с cutoff 30 Гц.
"""

from __future__ import annotations

import argparse
import itertools
import json
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
    sample_rate_hz,
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
# Протокол 4.2: машина тронулась, если горизонтальное ускорение выросло на 0.03 g.
ESC_START_ACCEL_G = 0.03
ESC_STEP_MIN_MS = 1500.0  # ступень газа короче — не ступень (протокол: по 2 с)
ESC_ACCEL_SMOOTH_MS = 100.0
# Протокол 7.2: добавка с компенсацией > 0.05 в гору, < 0 под гору, < 0.02 на ровном.
PITCH_UPHILL_MIN_ADD = 0.05
PITCH_FLAT_MAX_ADD = 0.02
PITCH_SLOPE_DEG = 5.0   # |тангаж| больше — склон
PITCH_FLAT_DEG = 2.0    # |тангаж| меньше — ровно
PITCH_WINDOW_MS = 500.0
PITCH_MIN_STICK = 0.1
PITCH_MAX_SPREAD_DEG = 1.0  # окно, где тангаж ещё сходится, не годится
PITCH_CMD_SATURATED = 0.99   # cmd_throttle на упоре: добавку не измерить
# Мёртвая зона газа RC (ApplyRcThrottleDeadzone, control_loop_helpers.hpp):
# стик 0.5 даёт команду 0.457 ещё до стабилизации.
RC_THROTTLE_DEADZONE = 0.08
OVERSTEER_MIN_CUT = 0.01
OVERSTEER_MERGE_MS = 200.0

COM_MIN_RATE_DPS = 30.0
COM_SETTLE_MS = 3000.0    # начало круга — разгон, продольное ускорение сдвигает rx
COM_MIN_TURN_MS = 2000.0  # установившегося круга в каждую сторону
COM_RESIDUAL_LIMIT_M = 0.05
COM_OFFSET_LIMIT_M = 0.3  # протокол 7.6

TILT_OK_DEG = 1.0
TILT_MIN_REST_MS = 2000.0
TILT_FINAL_MS = 500.0
TILT_TURN_RATE_DPS = 30.0

LPF_BAND_HZ = 10.0
LPF_NPERSEG = 256
LPF_FIT_SMOOTH_MS = 250.0
LPF_FIT_MIN_CORR = 0.95
LPF_OVERSHOOT_TOL = 0.05     # доля уставки
LPF_SETTLING_TOL_MS = 20.0   # два отсчёта лога 100 Гц
# Ошибки чтения IMU (протокол 5.1, LOS-383): событие ImuReadStats пишется раз
# в config::ImuConfig::kReadStatsEventIntervalMs и в том числе с нулями.
IMU_STATS_PERIOD_MS = 10000.0
IMU_ERRORS_MIN_COVER_S = 60.0
# Признак потери сцепления (протокол 7.5f, LOS-378): константы
# common/grip_loss_detector.hpp. Подгонка ищет kMaxSteerRad и
# kUndersteerGradient, при которых круги на асфальте не дают ложный grip_lost.
GRIP_WHEELBASE_M = 0.30
GRIP_FW_MAX_STEER_RAD = 0.356047
GRIP_FW_K = 0.0
GRIP_MAX_DEV_RATIO = 0.15
GRIP_MIN_DEV_RPS = 0.15
GRIP_WINDOW_MS = 300.0
GRIP_MIN_SPEED_MS = 0.8
GRIP_MIN_STEER = 0.15
GRIP_MIN_RATE_RPS = 0.3
GRIP_MAX_STEER_SPREAD = 0.02
GRIP_MAX_RATE_SPREAD = 0.03  # рад/с
GRIP_MIN_WINDOWS = 10
GRIP_MIN_SPEED_SPAN = 0.8  # м/с: разброс vx, без которого K не отделить
GRIP_FIT_MIN_DEG = 10.0
GRIP_FIT_MAX_DEG = 35.0
GRIP_MAX_FALSE_SHARE = 0.05


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


def rc_stick_command(rc_throttle: float) -> float:
    """Команда газа из стика RC до стабилизации (ApplyRcThrottleDeadzone)."""
    mag = abs(rc_throttle)
    if mag <= RC_THROTTLE_DEADZONE:
        return 0.0
    return math.copysign((mag - RC_THROTTLE_DEADZONE) / (1.0 - RC_THROTTLE_DEADZONE),
                         rc_throttle)


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
    # Направление шага — по отклонению от команды до шага, а не по абсолютному
    # значению: при перекладке из удержанного поворота старая команда больше.
    base = seg[0]
    peak_at = max(range(len(seg)),
                  key=lambda k: 0.0 if math.isnan(seg[k]) else abs(seg[k] - base))
    sign = math.copysign(1.0, seg[peak_at] - base) if seg[peak_at] != base else 1.0
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


def yaw_step_responses(rows: list[Row], gain_dps: float | None = None
                       ) -> list[tuple[int, dict | str]]:
    """(начало шага, отклик или причина пропуска) для каждого шага руля.

    `gain_dps=None` — уставка Normal-режима (90 °/с на единицу руля); в логе с
    колонкой `drive_mode` шаги в других режимах пропускаются: их коэффициент
    другой, его задают явно через `--gain-dps`.
    """
    steps = find_steering_steps(rows)
    ts = _ts(rows)
    mode = col_raw(rows, "drive_mode") if has_col(rows, "drive_mode") else None
    out: list[tuple[int, dict | str]] = []
    for n, start in enumerate(steps, 1):
        if gain_dps is None and mode is not None and mode[start + 1] != 0:
            out.append((start, (f"режим {mode[start + 1]:.0f} не Normal: "
                                "уставка не 90 °/с, задайте --gain-dps")))
            continue
        gain = NORMAL_STEER_TO_YAW_DPS if gain_dps is None else gain_dps
        # Окно кончается на следующем шаге, чтобы не мерить чужой переходный процесс.
        window = 1000.0
        if n < len(steps):
            window = min(window, ts[steps[n]] - ts[start])
        try:
            out.append((start, yaw_step_response(rows, start, window_ms=window,
                                                 gain_dps=gain)))
        except ValueError as exc:
            out.append((start, str(exc)))
    return out


def check_yaw_step(rows: list[Row], gain_dps: float | None = None
                   ) -> list[MetricResult]:
    """Overshoot/settling для каждого шага руля из `rc_steering`."""
    name = "Yaw PID step"
    if not has_col(rows, "yaw_rate_dps"):
        return [MetricResult(name, False, "НЕТ КОЛОНКИ yaw_rate_dps", no_data=True)]
    responses = yaw_step_responses(rows, gain_dps)
    if not responses:
        return [MetricResult(
            name, False,
            "ШАГ РУЛЯ НЕ НАЙДЕН (в лог пишется только команда с пульта RC; при "
            "управлении по Wi-Fi её нет)", no_data=True)]
    ts = _ts(rows)
    out = []
    for n, (start, r) in enumerate(responses, 1):
        label = f"{name} #{n} (t={ts[start]:.0f} мс)"
        if isinstance(r, str):
            out.append(MetricResult(label, None, r, no_data=True))
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
# ESC: минимальный газ старта (протокол 4.2)
# ---------------------------------------------------------------------------

def _moving_average(ts: list[float], v: list[float], window_ms: float) -> list[float]:
    out, acc, n, j = [], 0.0, 0, 0
    for i in range(len(v)):
        if not math.isnan(v[i]):
            acc += v[i]
            n += 1
        while ts[i] - ts[j] > window_ms:
            if not math.isnan(v[j]):
                acc -= v[j]
                n -= 1
            j += 1
        out.append(acc / n if n else math.nan)
    return out


def throttle_plateaus(rows: list[Row], min_ms: float = ESC_STEP_MIN_MS,
                      eps: float = 0.003) -> list[tuple[int, int, float]]:
    """Ступени применённого газа: (начало, конец, газ) длительностью ≥ min_ms."""
    ts, thr = _ts(rows), col_raw(rows, "throttle")
    out = []
    i = 0
    while i < len(rows):
        j = i
        while j + 1 < len(rows) and abs(thr[j + 1] - thr[i]) <= eps:
            j += 1
        if thr[i] > eps and ts[j] - ts[i] >= min_ms:
            out.append((i, j, thr[i]))
        i = j + 1
    return out


def esc_start_threshold(rows: list[Row]) -> dict:
    """Первая ступень газа, на которой машина тронулась.

    Тронулась — пик горизонтального ускорения √(ax²+ay²), сглаженного за
    100 мс, выше покоя (газ 0) на `ESC_START_ACCEL_G`. После калибровки покой
    приведён к (0, 0, 1), поэтому горизонталь не зависит от того, как IMU
    повёрнут в плоскости машины.
    """
    for c in ("throttle", "ax", "ay"):
        if not has_col(rows, c):
            raise ValueError(f"нет колонки {c}")
    ts, thr = _ts(rows), col_raw(rows, "throttle")
    ax, ay = col_raw(rows, "ax"), col_raw(rows, "ay")
    horiz = _moving_average(ts, [math.hypot(a, b) for a, b in zip(ax, ay)],
                            ESC_ACCEL_SMOOTH_MS)
    steps = throttle_plateaus(rows)
    if not steps:
        raise ValueError("ступеней газа ≥ 1.5 с не найдено")
    rest = _finite([horiz[i] for i in range(steps[0][0]) if abs(thr[i]) < 1e-3])
    if not rest:
        raise ValueError("нет покоя (газ 0) перед первой ступенью")
    rest.sort()
    base = rest[len(rest) // 2]
    table = []
    threshold = None
    for start, end, value in steps:
        peak = max(_finite(horiz[start:end + 1]), default=math.nan) - base
        table.append((value, peak))
        if threshold is None and peak > ESC_START_ACCEL_G:
            threshold = value
    return {"threshold": threshold, "steps": table, "rest_g": base}


def check_esc_start(rows: list[Row]) -> list[MetricResult]:
    name = "ESC: газ старта"
    try:
        r = esc_start_threshold(rows)
    except ValueError as exc:
        return [MetricResult(name, False, str(exc), no_data=True)]
    steps = ", ".join(f"{v:.2f}→{p:+.3f} g" for v, p in r["steps"])
    if r["threshold"] is None:
        return [MetricResult(name, False,
                             f"не тронулась до газа {r['steps'][-1][0]:.2f} ({steps})")]
    return [MetricResult(name, None,
                         f"порог {r['threshold']:.2f} (прирост ускорения > "
                         f"{ESC_START_ACCEL_G} g; ступени: {steps})")]


# ---------------------------------------------------------------------------
# Pitch compensation (протокол 7.2)
# ---------------------------------------------------------------------------

def pitch_comp_windows(rows: list[Row]) -> dict[str, list[float]]:
    """Добавка компенсации к газу по окнам 500 мс.

    Добавка — `cmd_throttle` минус команда стика после мёртвой зоны RC
    (`rc_stick_command`). В Normal между ними из газовых звеньев стоят только
    pitch compensation и oversteer guard (по умолчанию выключен), так что без
    компенсации добавка ≈ 0. Окно попадает в «подъём»/«спуск»/«ровно» по
    медиане `pitch_deg` (> 0 — нос вниз, stabilization_pipeline.cpp). Берутся
    окна с газом вперёд от пульта и устоявшимся тангажем (разброс < 1°):
    `pitch_deg` в логе — Madgwick, на разгоне он заваливается «носом вверх» и
    сходится к склону не сразу.
    """
    for c in ("rc_throttle", "cmd_throttle", "pitch_deg"):
        if not has_col(rows, c):
            raise ValueError(f"нет колонки {c}")
    ts = _ts(rows)
    rc, cmd = col_raw(rows, "rc_throttle"), col_raw(rows, "cmd_throttle")
    pitch = col_raw(rows, "pitch_deg")
    rc_ok = col_raw(rows, "rc_ok") if has_col(rows, "rc_ok") else [1.0] * len(rows)
    out: dict[str, list[float]] = {"uphill": [], "downhill": [], "flat": []}
    i = 0
    while i < len(rows):
        j = i
        while j + 1 < len(rows) and ts[j + 1] - ts[i] < PITCH_WINDOW_MS:
            j += 1
        idx = [k for k in range(i, j + 1)
               if rc_ok[k] == 1.0 and rc[k] >= PITCH_MIN_STICK
               and not math.isnan(cmd[k]) and not math.isnan(pitch[k])]
        if ts[j] - ts[i] >= 0.8 * PITCH_WINDOW_MS and len(idx) == j - i + 1:
            ps = sorted(pitch[k] for k in idx)
            # Упёрлись в полный газ — прошивка обрезала cmd до 1.0, и добавку
            # компенсации по такому окну не измерить.
            saturated = any(cmd[k] >= PITCH_CMD_SATURATED for k in idx)
            if ps[-1] - ps[0] > PITCH_MAX_SPREAD_DEG or saturated:
                i = j + 1
                continue
            p = ps[len(ps) // 2]
            add = sum(cmd[k] - rc_stick_command(rc[k]) for k in idx) / len(idx)
            nose_up = -p
            if nose_up > PITCH_SLOPE_DEG:
                out["uphill"].append(add)
            elif nose_up < -PITCH_SLOPE_DEG:
                out["downhill"].append(add)
            elif abs(p) < PITCH_FLAT_DEG:
                out["flat"].append(add)
        i = j + 1
    return out


def check_pitch_comp(rows: list[Row]) -> list[MetricResult]:
    name = "Pitch comp"
    try:
        w = pitch_comp_windows(rows)
    except ValueError as exc:
        return [MetricResult(name, False, str(exc), no_data=True)]
    if not any(w.values()):
        return [MetricResult(name, False, "НЕТ ОКОН С ГАЗОМ ВПЕРЁД ОТ ПУЛЬТА",
                             no_data=True)]
    out = []
    labels = {"uphill": "в гору", "downhill": "под гору", "flat": "ровно"}
    for key, ok, crit in (
            ("uphill", lambda a: a > PITCH_UPHILL_MIN_ADD, f"> {PITCH_UPHILL_MIN_ADD}"),
            ("downhill", lambda a: a < 0.0, "< 0"),
            ("flat", lambda a: abs(a) < PITCH_FLAT_MAX_ADD, f"|·| < {PITCH_FLAT_MAX_ADD}")):
        vals = w[key]
        label = f"{name}: {labels[key]}"
        if not vals:
            out.append(MetricResult(label, False, "окон нет", no_data=True))
            continue
        mean_add = sum(vals) / len(vals)
        out.append(MetricResult(
            label, ok(mean_add),
            f"добавка к газу {mean_add:+.3f} ({crit}), окон {len(vals)}"))
    return out


# ---------------------------------------------------------------------------
# Oversteer guard (протокол 7.3)
# ---------------------------------------------------------------------------

def oversteer_episodes(rows: list[Row]) -> list[dict]:
    """Эпизоды `oversteer_active` = 1 (разрывы < 200 мс склеиваются).

    Срез газа считается как «стик после мёртвой зоны минус cmd_throttle» и
    включает всё, что сделала стабилизация. Pitch comp тоже снижает газ, а
    её вклад в логе не виден (тангаж TiltEstimator не пишется), поэтому
    протокол 7.3 требует прогона с выключенной pitch comp.
    """
    for c in ("oversteer_active", "rc_throttle", "cmd_throttle", "slip_deg"):
        if not has_col(rows, c):
            raise ValueError(f"нет колонки {c}")
    ts = _ts(rows)
    act = col_raw(rows, "oversteer_active")
    rc, cmd = col_raw(rows, "rc_throttle"), col_raw(rows, "cmd_throttle")
    slip = col_raw(rows, "slip_deg")
    spans: list[list[int]] = []
    for i, a in enumerate(act):
        if a != 1.0:
            continue
        if spans and ts[i] - ts[spans[-1][1]] <= OVERSTEER_MERGE_MS:
            spans[-1][1] = i
        else:
            spans.append([i, i])
    out = []
    for a, b in spans:
        cuts = _finite([rc_stick_command(rc[k]) - cmd[k]
                        for k in range(a, b + 1) if act[k] == 1.0])
        slips = _finite([abs(slip[k]) for k in range(a, b + 1)])
        out.append({"t_ms": ts[a], "duration_ms": ts[b] - ts[a],
                    "max_cut": max(cuts, default=math.nan),
                    "max_slip_deg": max(slips, default=math.nan)})
    return out


def check_oversteer(rows: list[Row]) -> list[MetricResult]:
    name = "Oversteer guard"
    try:
        eps = oversteer_episodes(rows)
    except ValueError as exc:
        return [MetricResult(name, False, str(exc), no_data=True)]
    if not eps:
        return [MetricResult(name, False, "ЭПИЗОДОВ oversteer_active НЕТ", no_data=True)]
    out = []
    for n, e in enumerate(eps, 1):
        cut = e["max_cut"]
        out.append(MetricResult(
            f"{name} #{n} (t={e['t_ms']:.0f} мс)",
            not math.isnan(cut) and cut >= OVERSTEER_MIN_CUT,
            f"газ срезан на {cut:.3f} (стик − cmd ≥ {OVERSTEER_MIN_CUT}), "
            f"{e['duration_ms']:.0f} мс, max |slip| {e['max_slip_deg']:.1f}°"))
    return out


# ---------------------------------------------------------------------------
# События потери RC (протокол 2.5, LOS-216)
# ---------------------------------------------------------------------------

def _num(v) -> float:
    try:
        return float(v)
    except (TypeError, ValueError):
        return math.nan


def _cell(v) -> str:
    return "" if v is None else str(v)


def _event_value(cells: list[str], i: int) -> float:
    # Экспортёр web UI пишет и 0, и NaN пустой ячейкой (app.js, парсер
    # событий), а 0 штатный (у RcLost/RcRestored — нет импульсов, нет
    # дребезга; у ComCalibDone — нулевое смещение).
    # Пустую ячейку читаем как 0; NaN остаётся только для нечислового мусора.
    cell = cells[i].strip() if i < len(cells) else ""
    return 0.0 if cell == "" else _num(cell)


def log_events(rows: list[Row]) -> list[dict]:
    """События из колонок `event_*` (выгрузка web UI)."""
    if not has_col(rows, "event_type"):
        raise ValueError("нет колонки event_type (UDP-запись событий не содержит)")
    out = []
    for r in rows:
        # Несколько событий одного кадра web UI склеивает через «|» по всем
        # колонкам event_* (mergeFrameEvent в app.js) — разбираем по позициям.
        types = _cell(r.get("event_type")).split("|")
        params = _cell(r.get("event_param")).split("|")
        v1s = _cell(r.get("event_value1")).split("|")
        v2s = _cell(r.get("event_value2")).split("|")
        for i, ev in enumerate(types):
            if not ev:
                continue
            out.append({"type": ev, "t_ms": _num(r.get("ts_ms")),
                        "param": params[i] if i < len(params) else "",
                        "v1": _event_value(v1s, i), "v2": _event_value(v2s, i)})
    return out


def rc_link_events(rows: list[Row]) -> list[dict]:
    """События RcLost/RcRestored."""
    return [e for e in log_events(rows) if e["type"] in ("RcLost", "RcRestored")]


def check_rc_events(rows: list[Row]) -> list[MetricResult]:
    name = "RC-связь"
    try:
        events = rc_link_events(rows)
    except ValueError as exc:
        return [MetricResult(name, False, str(exc), no_data=True)]
    if not events:
        return [MetricResult(name, False, "СОБЫТИЙ RcLost/RcRestored НЕТ", no_data=True)]
    out = []
    lost = None
    n = 0
    for e in events:
        if e["type"] == "RcLost" and "since_boot" in e["param"]:
            # Пульт не включён при старте: прошивка не считает это потерей
            # (LossCount), парный RcRestored(first_acquire) тоже пропускаем.
            continue
        if e["type"] == "RcLost":
            if lost is not None:
                out.append(MetricResult(f"{name}: потеря #{n} (t={lost['t_ms']:.0f} мс)",
                                        False, "нет RcRestored до следующей потери"))
            n += 1
            lost = e
            continue
        if lost is None:
            if e["param"] != "first_acquire":
                out.append(MetricResult(f"{name}: RcRestored (t={e['t_ms']:.0f} мс)",
                                        False, "без предшествующей RcLost"))
            continue
        # Протокол 2.5 требует причину, ширины импульсов и длительность
        # потери: неполная диагностика — провал, а не «nan».
        # «unknown» — платформа не отдала счётчики декодера: причины и ширин
        # импульсов фактически нет.
        # Длительность 0 прошивка пишет только при первом захвате; у парного
        # восстановления пустая ячейка value1 — потерянное значение, а не 0 с.
        complete = (bool(lost["param"]) and "unknown" not in lost["param"]
                    and not any(math.isnan(x) for x in
                                (lost["v1"], lost["v2"], e["v1"], e["v2"]))
                    and e["v1"] > 0.0)
        out.append(MetricResult(
            f"{name}: потеря #{n} (t={lost['t_ms']:.0f} мс)", complete,
            f"{lost['param'] or 'причина не указана'}, импульсы thr/str "
            f"{lost['v1']:.0f}/{lost['v2']:.0f} мкс; восстановлена через "
            f"{e['v1']:.2f} с, дребезг {e['v2']:.0f}"))
        lost = None
    if lost is not None:
        # Восстановление и длительность потери не проверены — запись неполная.
        out.append(MetricResult(f"{name}: потеря #{n} (t={lost['t_ms']:.0f} мс)", False,
                                f"{lost['param']}, до конца лога не восстановлена"))
    if n == 0:
        # Остались только события старта без пульта: цикла потеря/восстановление
        # в записи нет, проверять нечего.
        out.append(MetricResult(name, False, "РЕАЛЬНЫХ ПОТЕРЬ RC НЕТ (только старт без пульта)",
                                no_data=True))
        return out
    out.append(MetricResult(f"{name}: всего потерь", None,
                            f"{n} — сверить с числом выключений пульта"))
    return out


def _whoami_count(param: str) -> float:
    # app.js пишет param ImuReadStats как «who=N», ноль — пустой ячейкой.
    cell = param.strip()
    if not cell:
        return 0.0
    return _num(cell.removeprefix("who="))


def check_imu_errors(rows: list[Row]) -> list[MetricResult]:
    """Протокол 5.1: ошибок чтения IMU 0 за минуту (события ImuReadStats).

    Каждое событие несёт счётчики за период с предыдущего, поэтому первое
    событие лога может захватить и время до начала записи — это в запас:
    ошибки до записи тоже считаются.
    """
    name = "Ошибки чтения IMU"
    try:
        events = [e for e in log_events(rows) if e["type"] == "ImuReadStats"]
    except ValueError as exc:
        return [MetricResult(name, False, str(exc), no_data=True)]
    if not events:
        return [MetricResult(name, False,
                             "СОБЫТИЙ ImuReadStats НЕТ (прошивка до LOS-383?)",
                             no_data=True)]
    missed = sum(e["v1"] for e in events)
    whoami = sum(_whoami_count(e["param"]) for e in events)
    run = max(e["v2"] for e in events)
    # Покрытие: период первого события плюс интервалы между событиями. Событие
    # приходит раз в период; если интервал длиннее, часть времени без
    # счётчиков (вытеснены из журнала или цикл стоял) — её не засчитываем.
    # Первое событие засчитываем только в пределах записи: если лог начат
    # (очищен, старые кадры вытеснены) меньше периода назад, остаток его
    # периода записью не подтверждён. Его ошибки всё равно считаются — в запас.
    first_ts = _ts(rows)[0]
    cover_ms = min(max(events[0]["t_ms"] - first_ts, 0.0), IMU_STATS_PERIOD_MS)
    gaps = 0
    for prev, cur in itertools.pairwise(events):
        dt = cur["t_ms"] - prev["t_ms"]
        if dt > 1.5 * IMU_STATS_PERIOD_MS:
            gaps += 1
        cover_ms += min(dt, IMU_STATS_PERIOD_MS)
    cover_s = cover_ms / 1000.0
    if any(math.isnan(x) for x in (missed, whoami, run)):
        return [MetricResult(name, False,
                             "нечисловые значения в событиях ImuReadStats")]
    errors = missed + whoami
    detail = (f"ошибок {errors:.0f} за {cover_s:.0f} с ({len(events)} событий): "
              f"пропущено семплов {missed:.0f}, WHO_AM_I {whoami:.0f}, "
              f"макс. серия {run:.0f}")
    if gaps:
        detail += f"; пропусков между событиями {gaps}"
    if cover_s < IMU_ERRORS_MIN_COVER_S:
        # Ошибка в короткой записи — уже провал; чистая короткая запись
        # минуты не доказывает.
        if errors > 0:
            return [MetricResult(name, False, detail)]
        return [MetricResult(name, False,
                             f"{detail}; покрыто меньше "
                             f"{IMU_ERRORS_MIN_COVER_S:.0f} с", no_data=True)]
    per_min = errors / (cover_s / 60.0)
    return [MetricResult(
        name, errors == 0,
        f"{detail}; {per_min:.1f}/мин (норма 0)")]


# ---------------------------------------------------------------------------
# Снапшоты конфигурации и события калибровок (выгрузка web UI)
# ---------------------------------------------------------------------------

def config_values(rows: list[Row], key: str,
                  section: str | None = "filter") -> list[float]:
    """Значения поля `<section>.<key>` из снапшотов `event_config`, по порядку.

    Web UI пишет снапшоты конфигурации стабилизации JSON-массивом в колонку
    `event_config` (parseConfigSnapshot в app.js): первый — на старте лога,
    дальше — при каждом изменении. Подряд идущие одинаковые значения
    склеиваются. section=None — поле верхнего уровня (`steering_trim`).
    """
    out: list[float] = []
    for r in rows:
        cell = _cell(r.get("event_config")).strip()
        if not cell:
            continue
        try:
            configs = json.loads(cell)
        except ValueError:
            continue
        for cfg in configs if isinstance(configs, list) else []:
            if not isinstance(cfg, dict):
                continue
            v = (cfg if section is None else cfg.get(section, {})).get(key)
            if v is None:
                continue
            v = float(v)
            if not out or out[-1] != v:
                out.append(v)
    return out


def _config_note(rows: list[Row], key: str, label: str, unit: str = "") -> str:
    vals = config_values(rows, key)
    if not vals:
        return f"{label} в логе нет"
    return f"{label} " + " → ".join(f"{v:g}" for v in vals) + unit


# ---------------------------------------------------------------------------
# CoM offset: остаточное смещение по кругам CW и CCW (протокол 7.6)
# ---------------------------------------------------------------------------

def com_residual(rows: list[Row]) -> dict:
    """Остаточное смещение IMU от центра масс по установившимся кругам.

    В логе `ax`/`ay` уже исправлены на сохранённый offset (CorrectForComOffset),
    поэтому на круге они = центростремительное ускорение ЦМ − ω²·r_ост. Как
    и в ComOffsetCalibration, круги в обе стороны: центростремительное
    меняет знак, а −ω²·r_ост нет, и r_ост = −(a_CW/ω²_CW + a_CCW/ω²_CCW)/2.
    Берётся `yaw_rate_dps` ≥ 30 °/с одного знака после 3 с поворота: в начале
    круга машина ещё разгоняется, и продольное ускорение сдвигает rx.
    Подходит лог самой калибровки CoM (она едет CW и CCW) или два круга с
    пульта с одинаковыми газом и рулём.
    """
    for c in ("ax", "ay", "yaw_rate_dps"):
        if not has_col(rows, c):
            raise ValueError(f"нет колонки {c}")
    ts = _ts(rows)
    ax, ay = col_raw(rows, "ax"), col_raw(rows, "ay")
    rate = col_raw(rows, "yaw_rate_dps")
    slip = col_raw(rows, "slip_deg") if has_col(rows, "slip_deg") else None
    acc: dict[int, dict[str, list[float]]] = {
        s: {"ax": [], "ay": [], "w2": [], "slip": []} for s in (1, -1)}
    turn_start, prev = 0.0, 0
    for i in range(len(rows)):
        r = rate[i]
        s = 0 if math.isnan(r) or abs(r) < COM_MIN_RATE_DPS else (1 if r > 0 else -1)
        if s != prev:
            turn_start, prev = ts[i], s
        if not s or ts[i] - turn_start < COM_SETTLE_MS:
            continue
        if math.isnan(ax[i]) or math.isnan(ay[i]):
            continue
        w = math.radians(r)
        acc[s]["ax"].append(ax[i] * G)
        acc[s]["ay"].append(ay[i] * G)
        acc[s]["w2"].append(w * w)
        if slip is not None and not math.isnan(slip[i]):
            acc[s]["slip"].append(abs(slip[i]))
    hz = sample_rate_hz(rows)
    dt = 1000.0 / hz if hz > 0 else 0.0
    for s, label in ((1, "влево (CCW)"), (-1, "вправо (CW)")):
        if len(acc[s]["w2"]) * dt < COM_MIN_TURN_MS:
            raise ValueError(
                f"нет установившегося круга {label}: нужно ≥ "
                f"{COM_MIN_TURN_MS / 1000:.0f} с с |yaw rate| ≥ "
                f"{COM_MIN_RATE_DPS:.0f} °/с после первых "
                f"{COM_SETTLE_MS / 1000:.0f} с поворота")

    def mean(v: list[float]) -> float:
        return sum(v) / len(v)

    def sem(v: list[float]) -> float:
        if len(v) < 2:
            return math.nan
        m = mean(v)
        return math.sqrt(sum((x - m) ** 2 for x in v) / (len(v) - 1) / len(v))

    per = {s: (mean(a["ax"]) / mean(a["w2"]), mean(a["ay"]) / mean(a["w2"]))
           for s, a in acc.items()}
    # Тряска на круге — десятые g: на логе 100 Гц это сантиметры в r.
    err = {k: math.hypot(*(sem(acc[s][k]) / mean(acc[s]["w2"]) for s in (1, -1))) / 2
           for k in ("ax", "ay")}
    slips = sorted(acc[1]["slip"] + acc[-1]["slip"])
    return {"rx": -(per[1][0] + per[-1][0]) / 2,
            "ry": -(per[1][1] + per[-1][1]) / 2,
            "rx_err": err["ax"], "ry_err": err["ay"],
            "rate_dps": {s: math.degrees(math.sqrt(mean(a["w2"])))
                         for s, a in acc.items()},
            "median_slip_deg": slips[len(slips) // 2] if slips else math.nan}


def com_calib_events(rows: list[Row]) -> list[dict]:
    """Итоги ComCalibDone/ComCalibFailed; rx/ry — в value1/value2 (LOS-382)."""
    return [e for e in log_events(rows) if e["type"] in ("ComCalibDone", "ComCalibFailed")]


def _com_residual_line(name: str, rows: list[Row], judge: bool) -> MetricResult:
    try:
        r = com_residual(rows)
    except ValueError as exc:
        return MetricResult(name, False, str(exc), no_data=True)
    norm = math.hypot(r["rx"], r["ry"])
    ok = norm <= COM_RESIDUAL_LIMIT_M
    slip = r["median_slip_deg"]
    return MetricResult(
        name, ok if judge else None,
        f"rx {r['rx']:+.3f} ± {r['rx_err']:.3f} м, ry {r['ry']:+.3f} ± "
        f"{r['ry_err']:.3f} м (|r| {norm:.3f} м"
        + (f", ≤ {COM_RESIDUAL_LIMIT_M} м{'' if ok else ', НЕ ВЫПОЛНЕНО'}" if judge else "")
        + f"); круги {r['rate_dps'][1]:.0f}/{r['rate_dps'][-1]:.0f} °/с, "
        f"медиана |slip| {slip:.2f}°")


def check_com_offset(rows: list[Row], baseline: list[Row] | None = None
                     ) -> list[MetricResult]:
    name = "CoM offset"
    out = []
    try:
        events = com_calib_events(rows)
    except ValueError as exc:
        events, why = [], str(exc)
    else:
        why = "событий ComCalibDone/ComCalibFailed нет: нужен лог самой калибровки"
    if not events:
        out.append(MetricResult(f"{name}: калибровка", False, why, no_data=True))
    for n, e in enumerate(events, 1):
        label = f"{name}: калибровка #{n} (t={e['t_ms']:.0f} мс)"
        if e["type"] == "ComCalibFailed":
            out.append(MetricResult(label, False, "failed"))
            continue
        ok = abs(e["v1"]) <= COM_OFFSET_LIMIT_M and abs(e["v2"]) <= COM_OFFSET_LIMIT_M
        out.append(MetricResult(
            label, ok, f"valid, rx {e['v1']:+.3f} м, ry {e['v2']:+.3f} м "
            f"(|·| ≤ {COM_OFFSET_LIMIT_M} м{'' if ok else ', НЕ ВЫПОЛНЕНО'})"))
    if baseline is not None:
        out.append(_com_residual_line(f"{name}: до калибровки (--baseline)",
                                      baseline, judge=False))
    out.append(_com_residual_line(f"{name}: остаточное смещение", rows,
                                  judge=True))
    return out


# ---------------------------------------------------------------------------
# Madgwick β: запаздывание и ложный наклон (протокол 7.7)
# ---------------------------------------------------------------------------

def tilt_errors(rows: list[Row]) -> dict:
    """Схождение pitch/roll Madgwick в покое и крен в повороте.

    Опора — сам Madgwick в конце покоя (среднее за последние 500 мс): к этому
    моменту он сходится к наклону площадки, а сравнивать с `ax/ay/az` нельзя —
    они в СК датчика, а `pitch_deg`/`roll_deg` в СК машины, и при наклонном
    монтаже IMU разница была бы постоянной. Расхождение в начале покоя — то,
    что Madgwick не догнал: малый β медленно сходится после склона или
    манёвра, большой успевает, но сильнее уводится манёвром (это видно по
    крену в повороте на ровном). Покой короче 2 с не годится: опоры нет.

    Опора по хвосту честная, потому что Madgwick нормирует градиент: в покое
    угол идёт к наклону по акселерометру со скоростью ~2β рад/с (5.7 °/с при
    β = 0.05), сколько бы ни осталось, и не застывает в стороне. Хвост, что
    ещё ползёт (размах > 0.25° за 500 мс, т. е. > 0.5 °/с), — «не сошёлся».
    """
    for c in ("pitch_deg", "roll_deg"):
        if not has_col(rows, c):
            raise ValueError(f"нет колонки {c}")
    ts = _ts(rows)
    pitch, roll = col_raw(rows, "pitch_deg"), col_raw(rows, "roll_deg")
    mask = detect_stationary_mask(rows)
    max_err, settle, rests = math.nan, 0.0, 0
    i = 0
    while i < len(rows):
        if not mask[i]:
            i += 1
            continue
        j = i
        while j + 1 < len(rows) and mask[j + 1]:
            j += 1
        seg = [k for k in range(i, j + 1)
               if not math.isnan(pitch[k]) and not math.isnan(roll[k])]
        if not seg or ts[seg[-1]] - ts[seg[0]] < TILT_MIN_REST_MS:
            i = j + 1
            continue
        rests += 1
        tail = [k for k in seg if ts[seg[-1]] - ts[k] < TILT_FINAL_MS]
        ref_p = sum(pitch[k] for k in tail) / len(tail)
        ref_r = sum(roll[k] for k in tail) / len(tail)
        err = {k: max(abs(pitch[k] - ref_p), abs(roll[k] - ref_r)) for k in seg}
        max_err = max(err.values()) if math.isnan(max_err) else max(
            max_err, *err.values())
        over = [k for k in seg if err[k] > TILT_OK_DEG]
        tail_p, tail_r = [pitch[k] for k in tail], [roll[k] for k in tail]
        if max(max(tail_p) - min(tail_p), max(tail_r) - min(tail_r)) > TILT_OK_DEG / 4:
            settle = math.inf  # ещё сходится в конце покоя: опора не годится
        elif over:
            settle = max(settle, ts[over[-1]] - ts[i])
        i = j + 1
    turn_roll: list[float] = []
    if has_col(rows, "yaw_rate_dps"):
        rate = col_raw(rows, "yaw_rate_dps")
        turn_roll = sorted(abs(roll[k]) for k in range(len(rows))
                           if abs(rate[k]) >= TILT_TURN_RATE_DPS
                           and not math.isnan(roll[k]))
    return {"rests": rests, "max_err_deg": max_err, "settle_ms": settle,
            "turn_roll_p95_deg": turn_roll[int(0.95 * (len(turn_roll) - 1))]
            if turn_roll else math.nan}


def _tilt_lines(name: str, rows: list[Row]) -> list[MetricResult]:
    note = _config_note(rows, "madgwick_beta", "β")
    adaptive = config_values(rows, "adaptive_beta_enabled")
    if any(adaptive):
        note += " (adaptive β включён: на манёвре β обнуляется, выключите)"
    try:
        t = tilt_errors(rows)
    except ValueError as exc:
        return [MetricResult(name, False, str(exc), no_data=True)]
    out = []
    if t["rests"]:
        settle = ("не сошёлся до конца покоя, нужен покой дольше"
                  if math.isinf(t["settle_ms"])
                  else f"сходится за {t['settle_ms']:.0f} мс")
        out.append(MetricResult(
            f"{name}: в покое", None,
            f"{note}; в начале покоя pitch/roll дальше от установившихся на "
            f"{t['max_err_deg']:.1f}°, {settle} (до {TILT_OK_DEG:.0f}°), "
            f"участков покоя {t['rests']}"))
    else:
        out.append(MetricResult(
            f"{name}: в покое", False,
            f"{note}; покоя ≥ {TILT_MIN_REST_MS / 1000:.0f} с нет", no_data=True))
    if math.isnan(t["turn_roll_p95_deg"]):
        out.append(MetricResult(f"{name}: в повороте", False,
                                f"поворотов ≥ {TILT_TURN_RATE_DPS:.0f} °/с нет",
                                no_data=True))
    else:
        out.append(MetricResult(
            f"{name}: в повороте", None,
            f"крен Madgwick (p95) {t['turn_roll_p95_deg']:.1f}° при |yaw rate| ≥ "
            f"{TILT_TURN_RATE_DPS:.0f} °/с — на ровном это ложный наклон"))
    return out


def check_madgwick_beta(rows: list[Row], baseline: list[Row] | None = None
                        ) -> list[MetricResult]:
    out = []
    if baseline is not None:
        out += _tilt_lines("Madgwick β (--baseline)", baseline)
    return out + _tilt_lines("Madgwick β", rows)


# ---------------------------------------------------------------------------
# LPF gz: подавление вибрации и шаг yaw rate (протокол 7.8)
# ---------------------------------------------------------------------------

def _fft(x: list[complex]) -> list[complex]:
    n = len(x)
    if n == 1:
        return list(x)
    even, odd = _fft(x[0::2]), _fft(x[1::2])
    out = [0j] * n
    for k in range(n // 2):
        t = complex(math.cos(-2 * math.pi * k / n), math.sin(-2 * math.pi * k / n)) * odd[k]
        out[k] = even[k] + t
        out[k + n // 2] = even[k] - t
    return out


def band_power(values: list[float], ts: list[float], rate_hz: float,
               f_lo_hz: float, nperseg: int = LPF_NPERSEG) -> float:
    """Мощность выше `f_lo_hz` по Уэлчу: окна Ханна, перекрытие 50 %.

    Окна с NaN или с неровным шагом `ts` (в логах PSRAM бывают задержки
    записи) пропускаются: скачок на стыке дал бы ложную мощность на всех
    частотах. Абсолютный масштаб не нужен — сравниваются сигналы одного лога.
    """
    if len(values) < nperseg:
        raise ValueError(f"лог короче {nperseg} отсчётов")
    step = 1000.0 / rate_hz
    even = [0.5 * step <= b - a <= 1.5 * step for a, b in zip(ts, ts[1:])]
    win = [0.5 - 0.5 * math.cos(2 * math.pi * k / nperseg) for k in range(nperseg)]
    k_lo = math.ceil(f_lo_hz * nperseg / rate_hz)
    total, segs = 0.0, 0
    for s in range(0, len(values) - nperseg + 1, nperseg // 2):
        seg = values[s:s + nperseg]
        if any(math.isnan(v) for v in seg) or not all(even[s:s + nperseg - 1]):
            continue
        mean = sum(seg) / nperseg
        spec = _fft([complex((v - mean) * w) for v, w in zip(seg, win)])
        total += sum(abs(spec[k]) ** 2 for k in range(k_lo, nperseg // 2 + 1))
        segs += 1
    if not segs:
        raise ValueError(f"нет окна в {nperseg} отсчётов без пропусков и "
                         "задержек записи")
    return total / segs


def raw_vehicle_yaw_rate(rows: list[Row]) -> list[float]:
    """Yaw rate машины до LPF из `gx/gy/gz` датчика.

    `yaw_rate_dps` — проекция гироскопа на вертикаль машины (gravity_vec
    калибровки), и уже потом LPF; `gz` — ось датчика, при наклонном монтаже
    в неё подмешаны крен и тангаж. Вертикали в логе нет, поэтому она
    оценивается по медленной части (скользящее среднее 250 мс, где LPF ≥ 5 Гц
    почти не влияет): yaw_rate_dps ≈ u·g, |u| = 1. При поворотах медленный
    вектор гироскопа лежит вдоль u, и u — нормированная взаимная корреляция
    Σ g·yaw_rate_dps; свободный МНК тут вырожден (gx, gy, gz коллинеарны).
    Если проекция плохо повторяет yaw_rate_dps, оси нет.
    """
    axes = [c for c in ("gx", "gy", "gz") if has_col(rows, c)]
    if "gz" not in axes or not has_col(rows, "yaw_rate_dps"):
        raise ValueError("нет колонок gz и yaw_rate_dps")
    ts = _ts(rows)
    raw = [col_raw(rows, c) for c in axes]
    yaw = col_raw(rows, "yaw_rate_dps")
    slow = [_moving_average(ts, v, LPF_FIT_SMOOTH_MS) for v in raw]
    slow_yaw = _moving_average(ts, yaw, LPF_FIT_SMOOTH_MS)
    idx = [k for k in range(len(rows))
           if not math.isnan(slow_yaw[k]) and not any(math.isnan(v[k]) for v in slow)]
    b = [sum(v[k] * slow_yaw[k] for k in idx) for v in slow]
    norm = math.hypot(*b)
    proj = [sum(bi * v[k] for bi, v in zip(b, slow)) / norm if norm else 0.0
            for k in idx]
    yy = sum(slow_yaw[k] ** 2 for k in idx)
    pp = sum(p * p for p in proj)
    corr = (sum(p * slow_yaw[k] for p, k in zip(proj, idx)) / math.sqrt(yy * pp)
            if yy > 0 and pp > 0 else 0.0)
    if corr < LPF_FIT_MIN_CORR:
        raise ValueError(f"ось yaw не найдена: в логе мало поворотов "
                         f"(корреляция {corr:.2f} < {LPF_FIT_MIN_CORR})")
    u = [bi / norm for bi in b]
    return [sum(w * v[k] for w, v in zip(u, raw)) for k in range(len(rows))]


def lpf_attenuation_db(rows: list[Row]) -> float:
    """Во сколько `yaw_rate_dps` тише yaw rate до LPF выше 10 Гц, дБ."""
    for c in ("gz", "yaw_rate_dps"):
        if not has_col(rows, c):
            raise ValueError(f"нет колонки {c}")
    rate = sample_rate_hz(rows)
    if rate <= 2 * LPF_BAND_HZ:
        raise ValueError(f"частота лога {rate:.0f} Гц: выше {LPF_BAND_HZ:.0f} Гц "
                         "спектра нет")
    ts = _ts(rows)
    raw = band_power(raw_vehicle_yaw_rate(rows), ts, rate, LPF_BAND_HZ)
    filt = band_power(col_raw(rows, "yaw_rate_dps"), ts, rate, LPF_BAND_HZ)
    if raw <= 0.0:
        raise ValueError(f"в сыром gz нет мощности выше {LPF_BAND_HZ:.0f} Гц")
    return 10.0 * math.log10(max(filt, 1e-30) / raw)


def yaw_step_summary(rows: list[Row], gain_dps: float | None = None) -> dict:
    """Худшие overshoot и settling по шагам руля в Normal: среднее или
    медиана спрятали бы один раскачавшийся шаг."""
    if not has_col(rows, "yaw_rate_dps"):
        raise ValueError("нет колонки yaw_rate_dps")
    over, settle = [], []
    for _, r in yaw_step_responses(rows, gain_dps):
        if isinstance(r, dict):
            over.append(r["overshoot"])
            settle.append(r["settling_ms"])
    if not over:
        raise ValueError("шагов руля в Normal нет")
    return {"steps": len(over), "overshoot": max(over),
            "settling_ms": max(settle)}


def check_lpf(rows: list[Row], baseline: list[Row] | None = None,
              gain_dps: float | None = None) -> list[MetricResult]:
    name = "LPF gz"
    out = []
    for label, log in ((f"{name} (--baseline)", baseline), (name, rows)):
        if log is None:
            continue
        note = _config_note(log, "lpf_cutoff_hz", "cutoff", " Гц")
        try:
            db = lpf_attenuation_db(log)
        except ValueError as exc:
            out.append(MetricResult(f"{label}: вибрация", False, f"{note}; {exc}",
                                    no_data=True))
            continue
        out.append(MetricResult(
            f"{label}: вибрация", None,
            f"{note}; мощность yaw_rate_dps выше {LPF_BAND_HZ:.0f} Гц "
            f"относительно yaw rate до LPF {db:+.1f} дБ"))
    try:
        cur = yaw_step_summary(rows, gain_dps)
    except ValueError as exc:
        return out + [MetricResult(f"{name}: шаг yaw rate", False, str(exc),
                                   no_data=True)]
    text = (f"худшие overshoot {cur['overshoot'] * 100:.1f} %, settling "
            f"{cur['settling_ms']:.0f} мс (шагов {cur['steps']})")
    if baseline is None:
        return out + [MetricResult(f"{name}: шаг yaw rate", None,
                                   text + "; эталон 30 Гц — через --baseline")]
    try:
        base = yaw_step_summary(baseline, gain_dps)
    except ValueError as exc:
        return out + [MetricResult(f"{name}: шаг yaw rate", False,
                                   f"{text}; эталон: {exc}", no_data=True)]
    ok_over = cur["overshoot"] <= base["overshoot"] + LPF_OVERSHOOT_TOL
    ok_settle = cur["settling_ms"] <= base["settling_ms"] + LPF_SETTLING_TOL_MS
    return out + [MetricResult(
        f"{name}: шаг yaw rate против эталона", ok_over and ok_settle,
        f"{text}; эталон {base['overshoot'] * 100:.1f} %, "
        f"{base['settling_ms']:.0f} мс (не хуже на {LPF_OVERSHOOT_TOL * 100:.0f} п.п. "
        f"и {LPF_SETTLING_TOL_MS:.0f} мс"
        f"{'' if ok_over and ok_settle else ', НЕ ВЫПОЛНЕНО'})")]


# ---------------------------------------------------------------------------
# Признак потери сцепления: подгонка δ_max и K по кругам (протокол 7.5f)
# ---------------------------------------------------------------------------

def grip_windows(rows: list[Row]) -> list[tuple[float, float, float]]:
    """Установившиеся участки поворота: (vx, руль колёс, yaw rate рад/с).

    Окно GRIP_WINDOW_MS годится, если руль в нём не гуляет больше
    GRIP_MAX_STEER_SPREAD, yaw rate — больше 5 % (и GRIP_MAX_RATE_SPREAD),
    а vx, |руль| и |yaw rate| не ниже порогов: на малых руле и скорости
    отношение r к рулю тонет в шуме. Руль колёс — `steering` минус
    `steering_trim` из снапшотов конфигурации: GripLossDetector получает руль
    без trim, иначе trim выглядел бы как поворот. Окна идут встык, без
    перекрытия.
    """
    for c in ("steering", "yaw_rate_dps"):
        if not has_col(rows, c):
            raise ValueError(f"нет колонки {c}")
    vcol = "vx" if has_col(rows, "vx") else "speed_ms"
    if not has_col(rows, vcol):
        raise ValueError("нет колонки vx/speed_ms")
    trims = config_values(rows, "steering_trim", section=None)
    trim = trims[-1] if trims else 0.0
    ts = _ts(rows)
    steer = [s - trim for s in col_raw(rows, "steering")]
    rate = [math.radians(r) for r in col_raw(rows, "yaw_rate_dps")]
    vx = col_raw(rows, vcol)
    out = []
    i = 0
    while i < len(rows):
        j = i
        while j < len(rows) and ts[j] - ts[i] < GRIP_WINDOW_MS:
            j += 1
        if j >= len(rows):
            break
        w = range(i, j)
        s_w = [steer[k] for k in w]
        r_w = [rate[k] for k in w]
        v_w = [vx[k] for k in w]
        i = j
        if any(math.isnan(x) for x in s_w + r_w + v_w):
            continue
        s_m, r_m, v_m = (sum(x) / len(x) for x in (s_w, r_w, v_w))
        if (v_m < GRIP_MIN_SPEED_MS or abs(s_m) < GRIP_MIN_STEER
                or abs(r_m) < GRIP_MIN_RATE_RPS or s_m * r_m <= 0.0):
            continue
        if (max(s_w) - min(s_w) > GRIP_MAX_STEER_SPREAD
                or max(r_w) - min(r_w) > max(0.05 * abs(r_m), GRIP_MAX_RATE_SPREAD)):
            continue
        out.append((v_m, s_m, r_m))
    return out


def grip_reference(v: float, steer: float, max_steer_rad: float, k: float) -> float:
    """Установившийся yaw rate по рулю (GripLossDetector::Update)."""
    return v * math.tan(steer * max_steer_rad) / (GRIP_WHEELBASE_M + k * v * v)


def grip_false_share(windows, max_steer_rad: float, k: float) -> float:
    """Доля окон, где |r − r_ref| выше допуска детектора: на асфальте —
    ложная потеря сцепления."""
    bad = 0
    for v, s, r in windows:
        ref = grip_reference(v, s, max_steer_rad, k)
        tol = max(GRIP_MAX_DEV_RATIO * abs(ref), GRIP_MIN_DEV_RPS)
        bad += abs(r - ref) > tol
    return bad / len(windows)


def grip_fit(windows) -> dict:
    """Подгонка δ_max и K: r = vx·tan(руль·δ_max) / (L + K·vx²).

    При заданном δ_max модель линейна по K: vx·tan(руль·δ_max)/r − L = K·vx²,
    K — МНК без свободного члена. δ_max перебирается по сетке 0.05°, берётся
    наименьшая сумма квадратов относительной ошибки r. K отделим от δ_max
    только по разным скоростям: при разбросе vx меньше GRIP_MIN_SPEED_SPAN
    K не подгоняется (= 0), и δ_max вбирает недостаточную поворачиваемость
    на этой скорости.
    """
    speeds = [v for v, _, _ in windows]
    fit_k = max(speeds) - min(speeds) >= GRIP_MIN_SPEED_SPAN
    best = None
    deg = GRIP_FIT_MIN_DEG
    while deg <= GRIP_FIT_MAX_DEG + 1e-9:
        d = math.radians(deg)
        k = 0.0
        if fit_k:
            num = sum((v * math.tan(s * d) / r - GRIP_WHEELBASE_M) * v * v
                      for v, s, r in windows)
            k = num / sum(v ** 4 for v, _, _ in windows)
        err = sum(((r - grip_reference(v, s, d, k)) / r) ** 2 for v, s, r in windows)
        if best is None or err < best[0]:
            best = (err, d, k)
        deg += 0.05
    err, d, k = best
    return {"max_steer_rad": d, "k": k, "k_fitted": fit_k,
            "rms_rel": math.sqrt(err / len(windows)),
            "speed_range": (min(speeds), max(speeds))}


def check_grip_fit(rows: list[Row]) -> list[MetricResult]:
    name = "Потеря сцепления"
    try:
        windows = grip_windows(rows)
    except ValueError as exc:
        return [MetricResult(name, False, str(exc), no_data=True)]
    if len(windows) < GRIP_MIN_WINDOWS:
        return [MetricResult(
            name, False,
            f"установившихся участков поворота {len(windows)}, нужно ≥ "
            f"{GRIP_MIN_WINDOWS}: круги с рулём ≥ {GRIP_MIN_STEER} на скорости "
            f"≥ {GRIP_MIN_SPEED_MS} м/с", no_data=True)]
    fw = grip_false_share(windows, GRIP_FW_MAX_STEER_RAD, GRIP_FW_K)
    ok = fw <= GRIP_MAX_FALSE_SHARE
    out = [MetricResult(
        f"{name}: константы прошивки", ok,
        f"δ_max {math.degrees(GRIP_FW_MAX_STEER_RAD):.1f}°, K {GRIP_FW_K:g}: "
        f"ложный grip_lost в {fw:.0%} окон (≤ {GRIP_MAX_FALSE_SHARE:.0%}"
        f"{'' if ok else ', НЕ ВЫПОЛНЕНО: подогнать по строке ниже'})")]
    fit = grip_fit(windows)
    fitted = grip_false_share(windows, fit["max_steer_rad"], fit["k"])
    lo, hi = fit["speed_range"]
    k_note = (f"K {fit['k']:.4f} с²/м" if fit["k_fitted"] else
              f"K не подогнан (vx {lo:.1f}–{hi:.1f} м/с, нужен разброс ≥ "
              f"{GRIP_MIN_SPEED_SPAN} м/с)")
    out.append(MetricResult(
        f"{name}: подгонка", None,
        f"kMaxSteerRad {fit['max_steer_rad']:.4f} "
        f"({math.degrees(fit['max_steer_rad']):.1f}°), {k_note}; "
        f"ошибка r {fit['rms_rel']:.1%} RMS, ложный grip_lost {fitted:.0%}; "
        f"{len(windows)} окон, vx {lo:.1f}–{hi:.1f} м/с"))
    return out


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

METRICS = ("yaw-step", "gyro-bias", "failsafe", "kids", "ekf-dist",
           "esc-start", "pitch-comp", "oversteer", "rc-events", "com-offset",
           "madgwick-beta", "lpf", "imu-errors", "grip-fit")


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
    if "esc-start" in wanted:
        out += check_esc_start(rows)
    if "pitch-comp" in wanted:
        out += check_pitch_comp(rows)
    if "oversteer" in wanted:
        out += check_oversteer(rows)
    if "rc-events" in wanted:
        out += check_rc_events(rows)
    if "imu-errors" in wanted:
        out += check_imu_errors(rows)
    if "grip-fit" in wanted:
        out += check_grip_fit(rows)
    baseline = kw.get("baseline")
    if "com-offset" in wanted:
        out += check_com_offset(rows, baseline)
    if "madgwick-beta" in wanted:
        out += check_madgwick_beta(rows, baseline)
    if "lpf" in wanted:
        out += check_lpf(rows, baseline, kw.get("gain_dps"))
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
    ap.add_argument("--baseline", metavar="CSV",
                    help="лог для сравнения: до калибровки (com-offset), другой β "
                         "(madgwick-beta), cutoff 30 Гц (lpf)")
    return ap.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.distance_m is not None and args.distance_m <= 0:
        print("ERROR: --distance-m должна быть > 0", file=sys.stderr)
        return 1
    logs = []
    for path in (args.csv_path, args.baseline):
        if path is None:
            logs.append(None)
            continue
        if not os.path.isfile(path):
            print(f"ERROR: файл не найден: {path}", file=sys.stderr)
            return 1
        _, log = load_csv(path)
        if not log or "ts_ms" not in log[0]:
            print(f"ERROR: пустой лог или нет колонки ts_ms: {path}", file=sys.stderr)
            return 1
        logs.append(log)
    rows, baseline = logs
    results = run_metrics(
        rows, args.only, args.distance_m, gain_dps=args.gain_dps,
        kids_max_speed=args.kids_max_speed, kids_max_accel=args.kids_max_accel,
        tolerance=args.tolerance, t_from_ms=args.from_ms, t_to_ms=args.to_ms,
        min_rest_s=args.min_rest_s, baseline=baseline)
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
