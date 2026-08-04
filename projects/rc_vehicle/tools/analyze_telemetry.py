#!/usr/bin/env python3
"""
RC Vehicle Telemetry Analyzer
Analyzes CSV telemetry data from TelemetryLogFrame and runs automated pass/fail checks.

Usage:
    python3 analyze_telemetry.py <path_to_csv> [--no-plots]
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import sys
from pathlib import Path
from typing import Any

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# Columns written by TelemetryLogFrame (firmware/common/telemetry_log.hpp).
# Keep this list in sync with that struct — the exporter emits them in order.
FRAME_COLUMNS = [
    "ts_ms", "ax", "ay", "az", "gx", "gy", "gz",
    "vx", "vy", "slip_deg", "speed_ms", "throttle", "steering",
    "pitch_deg", "roll_deg", "yaw_deg", "yaw_rate_dps", "oversteer_active",
    "rc_throttle", "rc_steering",
    "cmd_throttle", "cmd_steering",
    "ekf_vx_var", "ekf_vy_var", "ekf_r_var", "ekf_yaw_deg",
    "mx", "my", "mz", "heading_deg", "heading_rel_deg",
    "test_marker", "zupt_status", "ekf_diverged", "drive_mode", "stab_enabled",
    # Разложенная экспортёром маска kids_flags (LOS-13); в логах до LOS-13
    # этих колонок нет — отсутствие даёт предупреждение, но не ошибку.
    "kids_anti_spin_active", "kids_accel_limit_active",
    "kids_speed_limit_active", "kids_limiters_enabled",
]

# Event columns are stitched in by the exporter, are sparse, and `event_type`
# holds a symbolic name rather than a number — they are never parsed as floats.
EVENT_COLUMNS = ["event_type", "event_param", "event_value1", "event_value2",
                 "event_config"]

# Columns that must be present for the analyzer to say anything useful.
REQUIRED_COLUMNS = ["ts_ms", "throttle", "steering", "speed_ms"]

EXPECTED_COLUMNS = FRAME_COLUMNS + EVENT_COLUMNS

# Columns kept as raw strings (everything else is coerced to float/NaN).
TEXT_COLUMNS = {"event_type", "event_param", "event_config"}

# DriveMode enum (firmware/common/stabilization_config.hpp).
DRIVE_MODE_NAMES = {
    0: "Normal",
    1: "Sport",
    2: "Drift",
    3: "Kids",
    4: "DirectLaw",
}

ZUPT_STATUS_NAMES = {
    0: "not_evaluated",
    1: "applied",
    2: "throttle_rejected",
    3: "accel_rejected",
    4: "gyro_rejected",
}

# Stationary detection: |throttle| < threshold for at least 1 second of samples.
# The sample count is derived from the actual log rate (see stationary_min_samples).
STATIONARY_THROTTLE_THRESH = 0.05
STATIONARY_SPEED_THRESH = 0.15  # m/s — excludes coasting from "at rest"
STATIONARY_MIN_SECONDS = 1.0

# Firmware logging cadence: TelemetryLogConfig::kLogIntervalMs (config.hpp).
# A frame is pushed on the first control-loop tick at or after this interval,
# so the observed dt is quantised up to a multiple of the loop period.
LOG_INTERVAL_MS = 10.0
# A gap this many times the nominal interval means the control loop stalled.
STALL_FACTOR = 3.0
STALL_RATE_LIMIT = 0.5          # percent of samples allowed to be stalls
LOOP_HZ_NOMINAL = 500.0         # control loop design rate

# Check thresholds
GYRO_STD_LIMIT_DPS = 2.0        # gz std at rest
ACCEL_MAG_LOW = 0.95            # g
ACCEL_MAG_HIGH = 1.05           # g
ZUPT_VX_LIMIT = 0.1             # m/s
EKF_DRIFT_LIMIT = 0.1           # m/s
MADGWICK_PITCH_STD_LIMIT = 1.0  # degrees
MADGWICK_ROLL_STD_LIMIT = 1.0   # degrees
SLIP_AT_REST_LIMIT = 1.0        # degrees (when speed < 0.1 m/s)

# Steering authority: when the driver asks for this much lock ...
STEER_DEMAND_THRESH = 0.8
# ... the applied steering must reach at least this fraction of the demand,
# on at least STEER_AUTHORITY_MIN_RATIO of such samples.
STEER_ACHIEVED_FRACTION = 0.85
STEER_AUTHORITY_MIN_RATIO = 0.5

# Command chatter: a limiter that multiplies throttle after the slew stage
# produces single-sample drops and immediate recoveries. Flag a sample as
# chatter when the command drops by this much and recovers within two samples.
CHATTER_DROP = 0.25             # fraction of the pre-drop command
CHATTER_RATE_LIMIT = 1.0        # percent of moving samples allowed to chatter

# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------

Row = dict[str, Any]


def _parse_cell(column: str, value: str) -> Any:
    """Coerce one CSV cell.

    Text columns stay strings. Everything else becomes a float, with NaN for
    blanks — event columns are sparse, and silently substituting 0.0 for a
    missing value would corrupt every statistic computed over that column.
    """
    value = value.strip()
    if column in TEXT_COLUMNS:
        return value
    if not value:
        return math.nan
    try:
        return float(value)
    except ValueError:
        return math.nan


def load_csv(path: str) -> tuple[list[str], list[Row]]:
    """Load the telemetry CSV using only the stdlib.

    The previous pandas fast path was dropped: the two loaders disagreed on how
    blanks and the symbolic `event_type` column were represented, so results
    depended on whether pandas happened to be installed.
    """
    rows: list[Row] = []
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        columns = list(reader.fieldnames or [])
        for raw in reader:
            rows.append({c: _parse_cell(c, raw.get(c) or "") for c in columns})
    return columns, rows


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def col(rows: list[Row], name: str) -> list[float]:
    """Numeric column with NaNs dropped, so stats never poison on sparse data."""
    out: list[float] = []
    for r in rows:
        v = r.get(name)
        if isinstance(v, (int, float)) and not math.isnan(v):
            out.append(float(v))
    return out


def col_raw(rows: list[Row], name: str) -> list[float]:
    """Numeric column keeping NaNs, for index-aligned work (diffs, masks)."""
    out: list[float] = []
    for r in rows:
        v = r.get(name)
        out.append(float(v) if isinstance(v, (int, float)) else math.nan)
    return out


def has_col(rows: list[Row], name: str) -> bool:
    return bool(rows) and name in rows[0]


def mean(values: list[float]) -> float:
    if not values:
        return 0.0
    return sum(values) / len(values)


def percentile(values: list[float], q: float) -> float:
    """Linear-interpolated percentile (q in [0, 100]); 0.0 for empty input."""
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    pos = (len(ordered) - 1) * (q / 100.0)
    low = int(math.floor(pos))
    high = min(low + 1, len(ordered) - 1)
    return ordered[low] + (ordered[high] - ordered[low]) * (pos - low)


def variance(values: list[float]) -> float:
    if len(values) < 2:
        return 0.0
    m = mean(values)
    return sum((v - m) ** 2 for v in values) / (len(values) - 1)


def std(values: list[float]) -> float:
    return math.sqrt(variance(values))


def abs_max(values: list[float]) -> float:
    if not values:
        return 0.0
    return max(abs(v) for v in values)


def wrap_180(deg: float) -> float:
    """Wrap angle to [-180, 180] range."""
    deg = deg % 360.0
    if deg > 180.0:
        deg -= 360.0
    return deg


def angular_std(values: list[float]) -> float:
    """Compute std of angles handling ±180° wrapping via circular statistics."""
    if len(values) < 2:
        return 0.0
    rads = [math.radians(v) for v in values]
    s = sum(math.sin(r) for r in rads) / len(rads)
    c = sum(math.cos(r) for r in rads) / len(rads)
    r_len = math.sqrt(s**2 + c**2)
    # Circular variance = 1 - R, convert to approximate std in degrees
    if r_len >= 1.0:
        return 0.0
    return math.degrees(math.sqrt(-2.0 * math.log(r_len)))


# ---------------------------------------------------------------------------
# Stationary segment detection
# ---------------------------------------------------------------------------

def sample_rate_hz(rows: list[Row]) -> float:
    """Median-based sample rate; robust to the periodic stalls in real logs."""
    ts = col_raw(rows, "ts_ms")
    deltas = [b - a for a, b in zip(ts, ts[1:]) if not math.isnan(a) and not math.isnan(b) and b > a]
    if not deltas:
        return 0.0
    med = percentile(deltas, 50)
    return 1000.0 / med if med > 0 else 0.0


def stationary_min_samples(rows: list[Row]) -> int:
    """How many consecutive quiet samples make one second at the actual rate.

    The old fixed count of 20 assumed a 20 Hz log; current firmware logs at
    ~100 Hz, where 20 samples is only 0.2 s and lets brief throttle-offs
    through as "stationary".
    """
    rate = sample_rate_hz(rows)
    if rate <= 0:
        return 20
    return max(5, int(round(rate * STATIONARY_MIN_SECONDS)))


def detect_stationary_mask(rows: list[Row]) -> list[bool]:
    """Return a boolean mask: True where the sample belongs to a stationary segment."""
    throttle = col_raw(rows, "throttle")
    n = len(throttle)
    min_samples = stationary_min_samples(rows)

    # Throttle alone is not enough: a coasting car has zero throttle while still
    # rolling, and folding those samples into the "at rest" set is what made the
    # gyro/Madgwick noise checks report driving dynamics as sensor noise.
    speed = col_raw(rows, "speed_ms") if has_col(rows, "speed_ms") else [0.0] * n

    # First pass: mark samples where the car is neither driven nor moving
    quiet = [
        (not math.isnan(t))
        and abs(t) < STATIONARY_THROTTLE_THRESH
        and (math.isnan(s) or s < STATIONARY_SPEED_THRESH)
        for t, s in zip(throttle, speed)
    ]

    # Second pass: keep only runs of at least min_samples consecutive quiet samples
    mask = [False] * n
    i = 0
    while i < n:
        if quiet[i]:
            j = i
            while j < n and quiet[j]:
                j += 1
            run_len = j - i
            if run_len >= min_samples:
                for k in range(i, j):
                    mask[k] = True
            i = j
        else:
            i += 1
    return mask


def mode_segments(rows: list[Row]) -> list[tuple[int, int, int]]:
    """Split the log into (drive_mode, start_idx, end_idx_exclusive) runs."""
    if not has_col(rows, "drive_mode"):
        return [(0, 0, len(rows))]
    modes = col_raw(rows, "drive_mode")
    segments: list[tuple[int, int, int]] = []
    start = 0
    for i in range(1, len(modes) + 1):
        if i == len(modes) or modes[i] != modes[start]:
            mode = 0 if math.isnan(modes[start]) else int(modes[start])
            segments.append((mode, start, i))
            start = i
    return segments


def mode_mask(rows: list[Row], mode: int) -> list[bool]:
    modes = col_raw(rows, "drive_mode")
    return [(not math.isnan(m)) and int(m) == mode for m in modes]


def and_mask(a: list[bool], b: list[bool]) -> list[bool]:
    return [x and y for x, y in zip(a, b)]


def filter_by_mask(values: list[float], mask: list[bool]) -> list[float]:
    """Select masked samples, dropping NaNs so downstream stats stay finite."""
    return [v for v, m in zip(values, mask) if m and not math.isnan(v)]


# ---------------------------------------------------------------------------
# Automated checks
# ---------------------------------------------------------------------------

CheckResult = tuple[str, bool, str]  # (description, passed, measured_value_str)


def check_gyro_stability(rows: list[Row], mask: list[bool]) -> CheckResult:
    name = "Gyro stability at rest (std gz)"
    gz_static = filter_by_mask(col_raw(rows, "gz"), mask)
    if not gz_static:
        return name, False, "NO STATIONARY DATA"
    s = std(gz_static)
    passed = s < GYRO_STD_LIMIT_DPS
    return name, passed, f"std(gz)={s:.4f} dps  (limit < {GYRO_STD_LIMIT_DPS} dps)"


def check_accel_magnitude(rows: list[Row], mask: list[bool]) -> CheckResult:
    name = "Accelerometer ~1g at rest (mean |a|)"
    ax = filter_by_mask(col_raw(rows, "ax"), mask)
    ay = filter_by_mask(col_raw(rows, "ay"), mask)
    az = filter_by_mask(col_raw(rows, "az"), mask)
    if not ax:
        return name, False, "NO STATIONARY DATA"
    mags = [math.sqrt(x**2 + y**2 + z**2) for x, y, z in zip(ax, ay, az)]
    m = mean(mags)
    passed = ACCEL_MAG_LOW <= m <= ACCEL_MAG_HIGH
    return name, passed, f"mean(|a|)={m:.4f} g  (expected [{ACCEL_MAG_LOW}, {ACCEL_MAG_HIGH}] g)"


def check_zupt(rows: list[Row], mask: list[bool]) -> CheckResult:
    name = "ZUPT: vx near 0 at rest"
    vx_static = filter_by_mask(col_raw(rows, "vx"), mask)
    if not vx_static:
        return name, False, "NO STATIONARY DATA"
    m = abs_max(vx_static)
    passed = m < ZUPT_VX_LIMIT
    return name, passed, f"max(|vx|)={m:.6f} m/s  (limit < {ZUPT_VX_LIMIT} m/s)"


def check_ekf_drift(rows: list[Row], mask: list[bool]) -> CheckResult:
    name = "EKF no drift at rest (max |vx|)"
    vx_static = filter_by_mask(col_raw(rows, "vx"), mask)
    if not vx_static:
        return name, False, "NO STATIONARY DATA"
    m = abs_max(vx_static)
    passed = m < EKF_DRIFT_LIMIT
    return name, passed, f"max(|vx|)={m:.6f} m/s  (limit < {EKF_DRIFT_LIMIT} m/s)"


def check_zupt_status(rows: list[Row], mask: list[bool]) -> CheckResult:
    name = "ZUPT applied at rest"
    if "zupt_status" not in rows[0]:
        return name, False, "NO ZUPT STATUS COLUMN"

    statuses = [int(v) for v in filter_by_mask(col_raw(rows, "zupt_status"), mask)]
    if not statuses:
        return name, False, "NO STATIONARY DATA"

    counts = {status: statuses.count(status) for status in sorted(set(statuses))}
    summary = ", ".join(
        f"{ZUPT_STATUS_NAMES.get(status, 'unknown')}={count}"
        for status, count in counts.items()
    )
    applied = counts.get(1, 0)
    passed = applied > 0
    return name, passed, f"applied={applied}/{len(statuses)}; {summary}"


def check_madgwick_stable(rows: list[Row], mask: list[bool]) -> CheckResult:
    name = "Madgwick stable at rest (std pitch, std roll)"
    pitch_static = filter_by_mask(col_raw(rows, "pitch_deg"), mask)
    roll_static = filter_by_mask(col_raw(rows, "roll_deg"), mask)
    if not pitch_static:
        return name, False, "NO STATIONARY DATA"
    sp = angular_std(pitch_static)
    sr = angular_std(roll_static)
    passed = sp < MADGWICK_PITCH_STD_LIMIT and sr < MADGWICK_ROLL_STD_LIMIT
    return (
        name,
        passed,
        f"std(pitch)={sp:.4f}°  std(roll)={sr:.4f}°  "
        f"(limits < {MADGWICK_PITCH_STD_LIMIT}°, < {MADGWICK_ROLL_STD_LIMIT}°)",
    )


def check_no_false_oversteer(rows: list[Row], mask: list[bool]) -> CheckResult:
    name = "No false oversteer at rest"
    ov_static = filter_by_mask(col_raw(rows, "oversteer_active"), mask)
    if not ov_static:
        return name, False, "NO STATIONARY DATA"
    max_ov = max(ov_static)
    passed = max_ov == 0.0
    return name, passed, f"max(oversteer_active)={int(max_ov)}  (expected 0)"


def check_slip_at_rest(rows: list[Row]) -> CheckResult:
    name = "Slip angle ~0 when nearly stopped"
    speed = col_raw(rows, "speed_ms")
    slip = col_raw(rows, "slip_deg")
    # Check slip at stationary segments only (throttle ≈ 0, speed ≈ 0).
    # At very low speeds slip angle is numerically unstable (atan2(vy,vx) with both ≈ 0).
    throttle = col_raw(rows, "throttle")
    slow_slip = [
        wrap_180(s)
        for s, sp, t in zip(slip, speed, throttle)
        if not math.isnan(s) and not math.isnan(sp) and not math.isnan(t)
        and sp < 0.1 and abs(t) < STATIONARY_THROTTLE_THRESH
    ]
    if not slow_slip:
        return name, False, "NO STATIONARY LOW-SPEED DATA"
    m = abs_max(slow_slip)
    passed = m < SLIP_AT_REST_LIMIT
    return name, passed, f"max(|slip_deg|)={m:.4f}°  (limit < {SLIP_AT_REST_LIMIT}°)"


def check_log_cadence(rows: list[Row]) -> CheckResult:
    """Detect control-loop stalls via gaps in the logging cadence.

    The firmware pushes a frame on the first loop tick at or after
    kLogIntervalMs, so a healthy 500 Hz loop yields a near-constant 10 ms dt.
    Sustained larger dt means the loop runs slower than nominal; isolated large
    gaps mean it blocked.
    """
    name = "Log cadence / control-loop stalls"
    ts = col_raw(rows, "ts_ms")
    deltas = [b - a for a, b in zip(ts, ts[1:]) if not math.isnan(a) and not math.isnan(b)]
    if not deltas:
        return name, False, "NO TIMESTAMPS"

    stall_limit = LOG_INTERVAL_MS * STALL_FACTOR
    stall_times = [
        b
        for a, b in zip(ts, ts[1:])
        if not math.isnan(a) and not math.isnan(b) and (b - a) >= stall_limit
    ]
    stalls = [d for d in deltas if d >= stall_limit]
    stall_pct = 100.0 * len(stalls) / len(deltas)
    med = percentile(deltas, 50)
    worst = max(deltas)

    # A handful of scattered stalls is scheduling noise; evenly spaced ones mean
    # a periodic task blocks the control loop, which no stall rate should excuse.
    periodic = ""
    if len(stall_times) >= 5:
        gaps = [b - a for a, b in zip(stall_times, stall_times[1:])]
        gap_med = percentile(gaps, 50)
        if gap_med > 0:
            # Judge by the share of gaps near the median, not the worst one: a
            # single stall spanning two frames splits one interval in two and
            # would otherwise mask an otherwise perfectly regular pattern.
            near = sum(1 for g in gaps if abs(g - gap_med) <= 0.25 * gap_med)
            if near / len(gaps) >= 0.8:
                periodic = f"  PERIODIC every {gap_med / 1000.0:.1f} s"

    passed = (
        not periodic
        and stall_pct <= STALL_RATE_LIMIT
        and med <= LOG_INTERVAL_MS * 1.5
    )
    return (
        name,
        passed,
        f"median dt={med:.1f} ms  p99={percentile(deltas, 99):.1f} ms  "
        f"max={worst:.1f} ms  stalls(>={stall_limit:.0f} ms)={len(stalls)} "
        f"({stall_pct:.2f}%, limit {STALL_RATE_LIMIT}%){periodic}",
    )


def check_loop_rate(rows: list[Row]) -> CheckResult:
    """Bound the control-loop period from the quantisation of the log dt.

    A frame is emitted on the first tick at or after LOG_INTERVAL_MS, so
    dt = ceil(LOG_INTERVAL_MS / T_loop) * T_loop and the overshoot above the
    nominal interval is always less than one loop period. The largest
    non-stall overshoot is therefore a lower bound on T_loop, i.e. an upper
    bound on the loop rate. A loop comfortably faster than the log interval
    lands on a flat dt == LOG_INTERVAL_MS and produces no overshoot at all.

    This only ever bounds the rate: ts_ms is quantised to whole milliseconds,
    so the estimate cannot resolve periods finer than ~1 ms. The firmware's own
    DIAG line reports the measured rate directly.
    """
    name = "Control loop keeps up with log interval"
    ts = col_raw(rows, "ts_ms")
    deltas = [
        b - a
        for a, b in zip(ts, ts[1:])
        if not math.isnan(a) and not math.isnan(b) and 0 < (b - a) < LOG_INTERVAL_MS * STALL_FACTOR
    ]
    if not deltas:
        return name, False, "NO TIMESTAMPS"

    avg = mean(deltas)
    # p99 rather than max: a single late tick should not set the bound.
    overshoot = percentile(deltas, 99) - LOG_INTERVAL_MS
    if overshoot <= 1.0:
        # Within timestamp quantisation — the loop outruns the log interval.
        return (
            name,
            True,
            f"mean dt={avg:.2f} ms, p99={percentile(deltas, 99):.0f} ms "
            f"(nominal {LOG_INTERVAL_MS:.0f} ms) → loop outruns log interval",
        )

    hz_upper = 1000.0 / overshoot
    passed = hz_upper >= LOOP_HZ_NOMINAL
    return (
        name,
        passed,
        f"mean dt={avg:.2f} ms, p99={percentile(deltas, 99):.0f} ms "
        f"(nominal {LOG_INTERVAL_MS:.0f} ms) → loop period >= {overshoot:.1f} ms, "
        f"rate <= ~{hz_upper:.0f} Hz  (design {LOOP_HZ_NOMINAL:.0f} Hz)",
    )


def check_steering_authority(rows: list[Row]) -> CheckResult:
    """Did full stick deflection actually reach the wheels, per drive mode?

    Catches slew-rate starvation: a low kids-mode slew_steering means a demand
    for full lock is never reached during a normal-length corner, even though
    the steering_limit clamp would have allowed it.
    """
    name = "Steering authority (demand → applied)"
    if not (has_col(rows, "rc_steering") and has_col(rows, "steering")):
        return name, False, "NO STEERING COLUMNS"

    demand = col_raw(rows, "rc_steering")
    applied = col_raw(rows, "steering")

    parts: list[str] = []
    worst_ratio = 1.0
    evaluated = False
    for mode in sorted({m for m, _, _ in mode_segments(rows)}):
        mask = mode_mask(rows, mode)
        pairs = [
            (d, a)
            for d, a, m in zip(demand, applied, mask)
            if m and not math.isnan(d) and not math.isnan(a) and abs(d) >= STEER_DEMAND_THRESH
        ]
        if len(pairs) < 10:
            continue
        evaluated = True
        reached = sum(1 for d, a in pairs if abs(a) >= abs(d) * STEER_ACHIEVED_FRACTION)
        ratio = reached / len(pairs)
        worst_ratio = min(worst_ratio, ratio)
        peak = max(abs(a) for _, a in pairs)
        label = DRIVE_MODE_NAMES.get(mode, f"mode{mode}")
        parts.append(f"{label}: {reached}/{len(pairs)} reached ({ratio * 100:.0f}%), peak |steer|={peak:.2f}")

    if not evaluated:
        return name, True, f"no samples with |rc_steering| >= {STEER_DEMAND_THRESH} — not exercised"

    passed = worst_ratio >= STEER_AUTHORITY_MIN_RATIO
    return name, passed, "; ".join(parts) + f"  (limit >= {STEER_AUTHORITY_MIN_RATIO * 100:.0f}%)"


def check_command_chatter(rows: list[Row]) -> CheckResult:
    """Detect throttle chatter from limiters applied after the slew stage.

    KidsModeProcessor smooths throttle first, then multiplies it by the
    anti-spin / accel / speed reductions. Those reductions are therefore not
    rate-limited, so a limiter that engages and releases between ticks shows up
    as a one-sample collapse of the command with an immediate recovery, while
    the driver's stick (rc_throttle) stays put.
    """
    name = "Throttle command smoothness (limiter chatter)"
    cmd_name = "cmd_throttle" if has_col(rows, "cmd_throttle") else "throttle"
    if not has_col(rows, cmd_name) or not has_col(rows, "rc_throttle"):
        return name, False, "NO THROTTLE COLUMNS"

    cmd = col_raw(rows, cmd_name)
    stick = col_raw(rows, "rc_throttle")

    parts: list[str] = []
    worst_pct = 0.0
    evaluated = False
    for mode in sorted({m for m, _, _ in mode_segments(rows)}):
        mask = mode_mask(rows, mode)
        events = 0
        moving = 0
        for i in range(1, len(cmd) - 1):
            if not mask[i]:
                continue
            a, b, c = cmd[i - 1], cmd[i], cmd[i + 1]
            s_prev, s_now = stick[i - 1], stick[i]
            if any(math.isnan(v) for v in (a, b, c, s_prev, s_now)):
                continue
            if a <= 0.05:
                continue
            moving += 1
            # Stick held steady, but the command collapsed and bounced back.
            if abs(s_now - s_prev) > 0.1:
                continue
            if b < a * (1.0 - CHATTER_DROP) and c > b * 1.2 and c >= a * (1.0 - CHATTER_DROP):
                events += 1
        if moving < 50:
            continue
        evaluated = True
        pct = 100.0 * events / moving
        worst_pct = max(worst_pct, pct)
        label = DRIVE_MODE_NAMES.get(mode, f"mode{mode}")
        parts.append(f"{label}: {events}/{moving} ({pct:.2f}%)")

    if not evaluated:
        return name, True, "not enough throttle-on samples to evaluate"

    passed = worst_pct <= CHATTER_RATE_LIMIT
    return name, passed, "; ".join(parts) + f"  (limit <= {CHATTER_RATE_LIMIT}%)"


def check_ekf_divergence(rows: list[Row]) -> CheckResult:
    name = "EKF divergence guard never fired"
    if not has_col(rows, "ekf_diverged"):
        return name, True, "no ekf_diverged column (older log format)"
    flags = col(rows, "ekf_diverged")
    if not flags:
        return name, False, "NO DATA"
    fired = sum(1 for v in flags if v > 0.0)
    passed = fired == 0
    return name, passed, f"diverged samples={fired}/{len(flags)} ({100.0 * fired / len(flags):.2f}%)"


def run_all_checks(rows: list[Row], mask: list[bool]) -> list[CheckResult]:
    return [
        check_gyro_stability(rows, mask),
        check_accel_magnitude(rows, mask),
        check_zupt(rows, mask),
        check_ekf_drift(rows, mask),
        check_zupt_status(rows, mask),
        check_madgwick_stable(rows, mask),
        check_no_false_oversteer(rows, mask),
        check_slip_at_rest(rows),
        check_ekf_divergence(rows),
        check_log_cadence(rows),
        check_loop_rate(rows),
        check_steering_authority(rows),
        check_command_chatter(rows),
    ]


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def print_check_table(results: list[CheckResult]) -> None:
    col_w_name = max(len(r[0]) for r in results) + 2
    col_w_status = 6
    header = f"{'Check':<{col_w_name}}  {'Status':<{col_w_status}}  Measured"
    separator = "-" * (len(header) + 20)
    print()
    print("=" * len(separator))
    print("  AUTOMATED CHECK RESULTS")
    print("=" * len(separator))
    print(header)
    print(separator)
    for name, passed, measured in results:
        status = "PASS" if passed else "FAIL"
        print(f"  {name:<{col_w_name}}  {status:<{col_w_status}}  {measured}")
    print(separator)
    passed_count = sum(1 for _, p, _ in results if p)
    total = len(results)
    overall = "ALL PASS" if passed_count == total else f"{total - passed_count} FAILED"
    print(f"  Summary: {passed_count}/{total} checks passed  —  {overall}")
    print("=" * len(separator))
    print()


def print_dataset_info(
    rows: list[Row], columns: list[str], mask: list[bool], csv_path: str
) -> None:
    n = len(rows)
    stationary_count = sum(mask)
    ts = col(rows, "ts_ms")
    duration_s = (ts[-1] - ts[0]) / 1000.0 if len(ts) > 1 else 0.0
    rate = sample_rate_hz(rows)

    missing = [c for c in FRAME_COLUMNS if c not in columns]
    extra = [c for c in columns if c not in EXPECTED_COLUMNS]

    print()
    print("Dataset info:")
    print(f"  File         : {csv_path}")
    print(f"  Rows         : {n}")
    print(f"  Duration     : {duration_s:.2f} s")
    print(f"  Sample rate  : {rate:.1f} Hz (median dt)")
    print(f"  Stationary   : {stationary_count} samples ({stationary_count / n * 100:.1f}%), "
          f"window {stationary_min_samples(rows)} samples")
    if missing:
        print(f"  Missing cols : {', '.join(missing)}")
    if extra:
        print(f"  Unknown cols : {', '.join(extra)}")

    segments = mode_segments(rows)
    if len(segments) > 1 or (segments and segments[0][0] != 0):
        print("  Drive modes  :")
        for mode, start, end in segments:
            label = DRIVE_MODE_NAMES.get(mode, f"mode{mode}")
            t0 = rows[start].get("ts_ms", 0.0)
            t1 = rows[end - 1].get("ts_ms", 0.0)
            t0 = 0.0 if not isinstance(t0, float) or math.isnan(t0) else t0
            t1 = 0.0 if not isinstance(t1, float) or math.isnan(t1) else t1
            print(f"    {label:<10} {t0 / 1000.0:8.1f} .. {t1 / 1000.0:8.1f} s  ({end - start} samples)")

    if has_col(rows, "event_type"):
        events: dict[str, int] = {}
        for r in rows:
            ev = r.get("event_type")
            if isinstance(ev, str) and ev:
                events[ev] = events.get(ev, 0) + 1
        if events:
            print("  Events       : " + ", ".join(f"{k}={v}" for k, v in sorted(events.items())))
    print()


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------

def generate_plots(rows: list[Row], csv_path: str) -> None:
    try:
        import matplotlib  # type: ignore[import-untyped]
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt  # type: ignore[import-untyped]
    except ImportError:
        print("  [plots] matplotlib not available, skipping plots")
        return

    output_dir = Path(csv_path).parent / "telemetry_plots"
    output_dir.mkdir(parents=True, exist_ok=True)

    ts_s = [t / 1000.0 for t in col_raw(rows, "ts_ms")]

    # ------------------------------------------------------------------
    # 1. IMU raw
    # ------------------------------------------------------------------
    fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    fig.suptitle("IMU Raw Data", fontsize=14)

    axes[0].plot(ts_s, col_raw(rows, "ax"), label="ax", linewidth=0.8)
    axes[0].plot(ts_s, col_raw(rows, "ay"), label="ay", linewidth=0.8)
    axes[0].plot(ts_s, col_raw(rows, "az"), label="az", linewidth=0.8)
    axes[0].set_ylabel("Acceleration (g)")
    axes[0].legend(loc="upper right")
    axes[0].grid(True, alpha=0.4)

    axes[1].plot(ts_s, col_raw(rows, "gx"), label="gx", linewidth=0.8)
    axes[1].plot(ts_s, col_raw(rows, "gy"), label="gy", linewidth=0.8)
    axes[1].plot(ts_s, col_raw(rows, "gz"), label="gz", linewidth=0.8)
    axes[1].set_ylabel("Angular rate (dps)")
    axes[1].set_xlabel("Time (s)")
    axes[1].legend(loc="upper right")
    axes[1].grid(True, alpha=0.4)

    fig.tight_layout()
    out = output_dir / "01_imu_raw.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  [plots] saved {out}")

    # ------------------------------------------------------------------
    # 2. Orientation
    # ------------------------------------------------------------------
    fig, ax = plt.subplots(figsize=(12, 5))
    fig.suptitle("Orientation (Madgwick)", fontsize=14)

    ax.plot(ts_s, col_raw(rows, "pitch_deg"), label="pitch", linewidth=0.8)
    ax.plot(ts_s, col_raw(rows, "roll_deg"), label="roll", linewidth=0.8)
    ax.plot(ts_s, col_raw(rows, "yaw_deg"), label="yaw", linewidth=0.8)
    ax.set_ylabel("Angle (degrees)")
    ax.set_xlabel("Time (s)")
    ax.legend(loc="upper right")
    ax.grid(True, alpha=0.4)

    fig.tight_layout()
    out = output_dir / "02_orientation.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  [plots] saved {out}")

    # ------------------------------------------------------------------
    # 3. EKF dynamics
    # ------------------------------------------------------------------
    fig, ax = plt.subplots(figsize=(12, 5))
    fig.suptitle("EKF Dynamics", fontsize=14)

    ax.plot(ts_s, col_raw(rows, "vx"), label="vx (m/s)", linewidth=0.8)
    ax.plot(ts_s, col_raw(rows, "vy"), label="vy (m/s)", linewidth=0.8)
    ax.plot(ts_s, col_raw(rows, "speed_ms"), label="speed (m/s)", linewidth=1.0, color="black")
    ax.set_ylabel("Velocity (m/s)")
    ax.set_xlabel("Time (s)")
    ax.legend(loc="upper right")
    ax.grid(True, alpha=0.4)

    fig.tight_layout()
    out = output_dir / "03_ekf_dynamics.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  [plots] saved {out}")

    # ------------------------------------------------------------------
    # 4. Control inputs
    # ------------------------------------------------------------------
    fig, ax = plt.subplots(figsize=(12, 5))
    fig.suptitle("Control Inputs", fontsize=14)

    ax.plot(ts_s, col_raw(rows, "throttle"), label="throttle", linewidth=0.8)
    ax.plot(ts_s, col_raw(rows, "steering"), label="steering", linewidth=0.8)
    ax.set_ylabel("Normalized command [-1, 1]")
    ax.set_xlabel("Time (s)")
    ax.legend(loc="upper right")
    ax.grid(True, alpha=0.4)

    fig.tight_layout()
    out = output_dir / "04_control.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  [plots] saved {out}")

    # ------------------------------------------------------------------
    # 5. Slip analysis (dual y-axis)
    # ------------------------------------------------------------------
    fig, ax1 = plt.subplots(figsize=(12, 5))
    fig.suptitle("Slip Analysis", fontsize=14)

    color_slip = "steelblue"
    ax1.set_ylabel("slip_deg (degrees)", color=color_slip)
    ax1.plot(ts_s, col_raw(rows, "slip_deg"), label="slip_deg", color=color_slip, linewidth=0.8)
    ax1.tick_params(axis="y", labelcolor=color_slip)
    ax1.set_xlabel("Time (s)")
    ax1.grid(True, alpha=0.3)

    ax2 = ax1.twinx()
    color_ov = "crimson"
    ax2.set_ylabel("oversteer_active (0/1)", color=color_ov)
    ax2.fill_between(
        ts_s,
        col_raw(rows, "oversteer_active"),
        alpha=0.35,
        color=color_ov,
        label="oversteer_active",
        step="post",
    )
    ax2.set_ylim(-0.1, 3.0)
    ax2.tick_params(axis="y", labelcolor=color_ov)

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="upper right")

    fig.tight_layout()
    out = output_dir / "05_slip_analysis.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  [plots] saved {out}")

    # ------------------------------------------------------------------
    # 6. Throttle → Speed correlation
    # ------------------------------------------------------------------
    fig, ax1 = plt.subplots(figsize=(12, 5))
    fig.suptitle("Throttle → Speed Correlation", fontsize=14)

    color_thr = "steelblue"
    ax1.set_ylabel("throttle", color=color_thr)
    ax1.plot(ts_s, col_raw(rows, "throttle"), label="throttle", color=color_thr, linewidth=0.8)
    ax1.tick_params(axis="y", labelcolor=color_thr)
    ax1.set_xlabel("Time (s)")
    ax1.grid(True, alpha=0.3)

    ax2 = ax1.twinx()
    color_spd = "darkorange"
    ax2.set_ylabel("speed (m/s)", color=color_spd)
    ax2.plot(ts_s, col_raw(rows, "speed_ms"), label="speed_ms", color=color_spd, linewidth=1.0)
    ax2.tick_params(axis="y", labelcolor=color_spd)

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="upper right")

    fig.tight_layout()
    out = output_dir / "06_throttle_speed.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  [plots] saved {out}")

    # ------------------------------------------------------------------
    # 7. Demand → command → applied (limiter / slew visibility)
    # ------------------------------------------------------------------
    if has_col(rows, "cmd_throttle"):
        fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
        fig.suptitle("Driver Demand vs Command vs Applied", fontsize=14)

        axes[0].plot(ts_s, col_raw(rows, "rc_throttle"), label="rc_throttle (stick)", linewidth=0.8)
        axes[0].plot(ts_s, col_raw(rows, "cmd_throttle"), label="cmd_throttle (post-limiters)", linewidth=0.8)
        axes[0].plot(ts_s, col_raw(rows, "throttle"), label="throttle (applied)", linewidth=0.8)
        axes[0].set_ylabel("Throttle")
        axes[0].legend(loc="upper right")
        axes[0].grid(True, alpha=0.4)

        axes[1].plot(ts_s, col_raw(rows, "rc_steering"), label="rc_steering (stick)", linewidth=0.8)
        axes[1].plot(ts_s, col_raw(rows, "cmd_steering"), label="cmd_steering (post-limiters)", linewidth=0.8)
        axes[1].plot(ts_s, col_raw(rows, "steering"), label="steering (applied)", linewidth=0.8)
        axes[1].set_ylabel("Steering")
        axes[1].set_xlabel("Time (s)")
        axes[1].legend(loc="upper right")
        axes[1].grid(True, alpha=0.4)

        fig.tight_layout()
        out = output_dir / "07_demand_vs_applied.png"
        fig.savefig(out, dpi=150)
        plt.close(fig)
        print(f"  [plots] saved {out}")

    # ------------------------------------------------------------------
    # 8. Logging cadence (control-loop health)
    # ------------------------------------------------------------------
    ts_ms = col_raw(rows, "ts_ms")
    deltas = [(b, b - a) for a, b in zip(ts_ms, ts_ms[1:]) if not math.isnan(a) and not math.isnan(b)]
    if deltas:
        fig, ax = plt.subplots(figsize=(12, 5))
        fig.suptitle("Logging Cadence (control-loop stalls)", fontsize=14)
        ax.plot([t / 1000.0 for t, _ in deltas], [d for _, d in deltas], linewidth=0.6)
        ax.axhline(LOG_INTERVAL_MS, color="green", linestyle="--", linewidth=1.0,
                   label=f"nominal {LOG_INTERVAL_MS:.0f} ms")
        ax.axhline(LOG_INTERVAL_MS * STALL_FACTOR, color="crimson", linestyle="--", linewidth=1.0,
                   label=f"stall threshold {LOG_INTERVAL_MS * STALL_FACTOR:.0f} ms")
        ax.set_ylabel("dt between frames (ms)")
        ax.set_xlabel("Time (s)")
        ax.legend(loc="upper right")
        ax.grid(True, alpha=0.4)

        fig.tight_layout()
        out = output_dir / "08_log_cadence.png"
        fig.savefig(out, dpi=150)
        plt.close(fig)
        print(f"  [plots] saved {out}")

    print(f"\n  All plots saved to: {output_dir}/")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Analyze RC vehicle telemetry CSV (TelemetryLogFrame format).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("csv_path", help="Path to the telemetry CSV file")
    parser.add_argument(
        "--no-plots",
        action="store_true",
        default=False,
        help="Skip plot generation even if matplotlib is available",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    csv_path = args.csv_path

    if not os.path.isfile(csv_path):
        print(f"ERROR: file not found: {csv_path}", file=sys.stderr)
        return 1

    print(f"\nLoading: {csv_path}")
    columns, rows = load_csv(csv_path)

    if not rows:
        print("ERROR: CSV is empty or could not be parsed.", file=sys.stderr)
        return 1

    # Warn about missing frame columns but continue; bail only if the log is
    # missing something every check depends on.
    missing = [c for c in FRAME_COLUMNS if c not in columns]
    if missing:
        print(f"  WARNING: missing expected columns: {', '.join(missing)}")
    fatal = [c for c in REQUIRED_COLUMNS if c not in columns]
    if fatal:
        print(f"ERROR: log lacks required columns: {', '.join(fatal)}", file=sys.stderr)
        return 1

    # Detect stationary segments
    mask = detect_stationary_mask(rows)
    stationary_count = sum(mask)
    print(f"  Stationary samples detected: {stationary_count} / {len(rows)}")

    # Dataset summary
    print_dataset_info(rows, columns, mask, csv_path)

    # Run checks
    results = run_all_checks(rows, mask)
    print_check_table(results)

    # Generate plots
    if not args.no_plots:
        print("Generating plots...")
        generate_plots(rows, csv_path)
    else:
        print("  [plots] skipped (--no-plots)")

    # Final summary exit code
    failed = [r for r in results if not r[1]]
    if failed:
        print(f"RESULT: {len(failed)} check(s) FAILED.\n")
        return 2
    print("RESULT: All checks PASSED.\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
