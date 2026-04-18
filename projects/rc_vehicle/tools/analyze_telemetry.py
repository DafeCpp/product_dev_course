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

EXPECTED_COLUMNS = [
    "ts_ms", "ax", "ay", "az", "gx", "gy", "gz",
    "vx", "vy", "slip_deg", "speed_ms", "throttle", "steering",
    "pitch_deg", "roll_deg", "yaw_deg", "yaw_rate_dps", "oversteer_active",
    "rc_throttle", "rc_steering",
]

# Stationary detection: |throttle| < 0.05 for at least this many consecutive samples.
# At ~20 Hz this is 1 second.
STATIONARY_THROTTLE_THRESH = 0.05
STATIONARY_MIN_SAMPLES = 20

# Check thresholds
GYRO_STD_LIMIT_DPS = 2.0        # gz std at rest
ACCEL_MAG_LOW = 0.95            # g
ACCEL_MAG_HIGH = 1.05           # g
ZUPT_VX_LIMIT = 0.1             # m/s
EKF_DRIFT_LIMIT = 0.1           # m/s
MADGWICK_PITCH_STD_LIMIT = 1.0  # degrees
MADGWICK_ROLL_STD_LIMIT = 1.0   # degrees
SLIP_AT_REST_LIMIT = 1.0        # degrees (when speed < 0.1 m/s)

# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------

Row = dict[str, float | int]


def _try_import_pandas() -> Any:
    try:
        import pandas as pd  # type: ignore[import-untyped]
        return pd
    except ImportError:
        return None


def load_csv_stdlib(path: str) -> tuple[list[str], list[Row]]:
    rows: list[Row] = []
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        columns = list(reader.fieldnames or [])
        for raw in reader:
            row: Row = {}
            for col in columns:
                val = raw.get(col, "").strip()
                try:
                    row[col] = float(val)
                except ValueError:
                    row[col] = 0.0
            rows.append(row)
    return columns, rows


def load_csv_pandas(path: str, pd: Any) -> tuple[list[str], list[Row]]:
    df = pd.read_csv(path)
    columns = list(df.columns)
    rows: list[Row] = df.to_dict(orient="records")
    return columns, rows


def load_csv(path: str) -> tuple[list[str], list[Row]]:
    pd = _try_import_pandas()
    if pd is not None:
        print("  [loader] pandas available, using pandas")
        return load_csv_pandas(path, pd)
    print("  [loader] pandas not available, falling back to stdlib csv module")
    return load_csv_stdlib(path)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def col(rows: list[Row], name: str) -> list[float]:
    return [float(r.get(name, 0.0)) for r in rows]


def mean(values: list[float]) -> float:
    if not values:
        return 0.0
    return sum(values) / len(values)


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

def detect_stationary_mask(rows: list[Row]) -> list[bool]:
    """Return a boolean mask: True where the sample belongs to a stationary segment."""
    throttle = col(rows, "throttle")
    n = len(throttle)

    # First pass: mark samples where |throttle| < threshold
    quiet = [abs(t) < STATIONARY_THROTTLE_THRESH for t in throttle]

    # Second pass: keep only runs of at least STATIONARY_MIN_SAMPLES consecutive quiet samples
    mask = [False] * n
    i = 0
    while i < n:
        if quiet[i]:
            j = i
            while j < n and quiet[j]:
                j += 1
            run_len = j - i
            if run_len >= STATIONARY_MIN_SAMPLES:
                for k in range(i, j):
                    mask[k] = True
            i = j
        else:
            i += 1
    return mask


def filter_by_mask(values: list[float], mask: list[bool]) -> list[float]:
    return [v for v, m in zip(values, mask) if m]


# ---------------------------------------------------------------------------
# Automated checks
# ---------------------------------------------------------------------------

CheckResult = tuple[str, bool, str]  # (description, passed, measured_value_str)


def check_gyro_stability(rows: list[Row], mask: list[bool]) -> CheckResult:
    name = "Gyro stability at rest (std gz)"
    gz_static = filter_by_mask(col(rows, "gz"), mask)
    if not gz_static:
        return name, False, "NO STATIONARY DATA"
    s = std(gz_static)
    passed = s < GYRO_STD_LIMIT_DPS
    return name, passed, f"std(gz)={s:.4f} dps  (limit < {GYRO_STD_LIMIT_DPS} dps)"


def check_accel_magnitude(rows: list[Row], mask: list[bool]) -> CheckResult:
    name = "Accelerometer ~1g at rest (mean |a|)"
    ax = filter_by_mask(col(rows, "ax"), mask)
    ay = filter_by_mask(col(rows, "ay"), mask)
    az = filter_by_mask(col(rows, "az"), mask)
    if not ax:
        return name, False, "NO STATIONARY DATA"
    mags = [math.sqrt(x**2 + y**2 + z**2) for x, y, z in zip(ax, ay, az)]
    m = mean(mags)
    passed = ACCEL_MAG_LOW <= m <= ACCEL_MAG_HIGH
    return name, passed, f"mean(|a|)={m:.4f} g  (expected [{ACCEL_MAG_LOW}, {ACCEL_MAG_HIGH}] g)"


def check_zupt(rows: list[Row], mask: list[bool]) -> CheckResult:
    name = "ZUPT: vx near 0 at rest"
    vx_static = filter_by_mask(col(rows, "vx"), mask)
    if not vx_static:
        return name, False, "NO STATIONARY DATA"
    m = abs_max(vx_static)
    passed = m < ZUPT_VX_LIMIT
    return name, passed, f"max(|vx|)={m:.6f} m/s  (limit < {ZUPT_VX_LIMIT} m/s)"


def check_ekf_drift(rows: list[Row], mask: list[bool]) -> CheckResult:
    name = "EKF no drift at rest (max |vx|)"
    vx_static = filter_by_mask(col(rows, "vx"), mask)
    if not vx_static:
        return name, False, "NO STATIONARY DATA"
    m = abs_max(vx_static)
    passed = m < EKF_DRIFT_LIMIT
    return name, passed, f"max(|vx|)={m:.6f} m/s  (limit < {EKF_DRIFT_LIMIT} m/s)"


def check_madgwick_stable(rows: list[Row], mask: list[bool]) -> CheckResult:
    name = "Madgwick stable at rest (std pitch, std roll)"
    pitch_static = filter_by_mask(col(rows, "pitch_deg"), mask)
    roll_static = filter_by_mask(col(rows, "roll_deg"), mask)
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
    ov_static = filter_by_mask(col(rows, "oversteer_active"), mask)
    if not ov_static:
        return name, False, "NO STATIONARY DATA"
    max_ov = max(ov_static)
    passed = max_ov == 0.0
    return name, passed, f"max(oversteer_active)={int(max_ov)}  (expected 0)"


def check_slip_at_rest(rows: list[Row]) -> CheckResult:
    name = "Slip angle ~0 when nearly stopped"
    speed = col(rows, "speed_ms")
    slip = col(rows, "slip_deg")
    # Check slip at stationary segments only (throttle ≈ 0, speed ≈ 0).
    # At very low speeds slip angle is numerically unstable (atan2(vy,vx) with both ≈ 0).
    throttle = col(rows, "throttle")
    slow_slip = [
        wrap_180(s)
        for s, sp, t in zip(slip, speed, throttle)
        if sp < 0.1 and abs(t) < STATIONARY_THROTTLE_THRESH
    ]
    if not slow_slip:
        return name, False, "NO STATIONARY LOW-SPEED DATA"
    m = abs_max(slow_slip)
    passed = m < SLIP_AT_REST_LIMIT
    return name, passed, f"max(|slip_deg|)={m:.4f}°  (limit < {SLIP_AT_REST_LIMIT}°)"


def run_all_checks(rows: list[Row], mask: list[bool]) -> list[CheckResult]:
    return [
        check_gyro_stability(rows, mask),
        check_accel_magnitude(rows, mask),
        check_zupt(rows, mask),
        check_ekf_drift(rows, mask),
        check_madgwick_stable(rows, mask),
        check_no_false_oversteer(rows, mask),
        check_slip_at_rest(rows),
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
    if n > 1:
        ts = col(rows, "ts_ms")
        duration_s = (ts[-1] - ts[0]) / 1000.0
        sample_rate = (n - 1) / duration_s if duration_s > 0 else 0.0
    else:
        duration_s = 0.0
        sample_rate = 0.0

    missing = [c for c in EXPECTED_COLUMNS if c not in columns]
    extra = [c for c in columns if c not in EXPECTED_COLUMNS]

    print()
    print("Dataset info:")
    print(f"  File         : {csv_path}")
    print(f"  Rows         : {n}")
    print(f"  Duration     : {duration_s:.2f} s")
    print(f"  Sample rate  : {sample_rate:.1f} Hz (estimated)")
    print(f"  Stationary   : {stationary_count} samples ({stationary_count / n * 100:.1f}%)")
    if missing:
        print(f"  Missing cols : {', '.join(missing)}")
    if extra:
        print(f"  Extra cols   : {', '.join(extra)}")
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

    ts_s = [t / 1000.0 for t in col(rows, "ts_ms")]

    # ------------------------------------------------------------------
    # 1. IMU raw
    # ------------------------------------------------------------------
    fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    fig.suptitle("IMU Raw Data", fontsize=14)

    axes[0].plot(ts_s, col(rows, "ax"), label="ax", linewidth=0.8)
    axes[0].plot(ts_s, col(rows, "ay"), label="ay", linewidth=0.8)
    axes[0].plot(ts_s, col(rows, "az"), label="az", linewidth=0.8)
    axes[0].set_ylabel("Acceleration (g)")
    axes[0].legend(loc="upper right")
    axes[0].grid(True, alpha=0.4)

    axes[1].plot(ts_s, col(rows, "gx"), label="gx", linewidth=0.8)
    axes[1].plot(ts_s, col(rows, "gy"), label="gy", linewidth=0.8)
    axes[1].plot(ts_s, col(rows, "gz"), label="gz", linewidth=0.8)
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

    ax.plot(ts_s, col(rows, "pitch_deg"), label="pitch", linewidth=0.8)
    ax.plot(ts_s, col(rows, "roll_deg"), label="roll", linewidth=0.8)
    ax.plot(ts_s, col(rows, "yaw_deg"), label="yaw", linewidth=0.8)
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

    ax.plot(ts_s, col(rows, "vx"), label="vx (m/s)", linewidth=0.8)
    ax.plot(ts_s, col(rows, "vy"), label="vy (m/s)", linewidth=0.8)
    ax.plot(ts_s, col(rows, "speed_ms"), label="speed (m/s)", linewidth=1.0, color="black")
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

    ax.plot(ts_s, col(rows, "throttle"), label="throttle", linewidth=0.8)
    ax.plot(ts_s, col(rows, "steering"), label="steering", linewidth=0.8)
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
    ax1.plot(ts_s, col(rows, "slip_deg"), label="slip_deg", color=color_slip, linewidth=0.8)
    ax1.tick_params(axis="y", labelcolor=color_slip)
    ax1.set_xlabel("Time (s)")
    ax1.grid(True, alpha=0.3)

    ax2 = ax1.twinx()
    color_ov = "crimson"
    ax2.set_ylabel("oversteer_active (0/1)", color=color_ov)
    ax2.fill_between(
        ts_s,
        col(rows, "oversteer_active"),
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
    ax1.plot(ts_s, col(rows, "throttle"), label="throttle", color=color_thr, linewidth=0.8)
    ax1.tick_params(axis="y", labelcolor=color_thr)
    ax1.set_xlabel("Time (s)")
    ax1.grid(True, alpha=0.3)

    ax2 = ax1.twinx()
    color_spd = "darkorange"
    ax2.set_ylabel("speed (m/s)", color=color_spd)
    ax2.plot(ts_s, col(rows, "speed_ms"), label="speed_ms", color=color_spd, linewidth=1.0)
    ax2.tick_params(axis="y", labelcolor=color_spd)

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="upper right")

    fig.tight_layout()
    out = output_dir / "06_throttle_speed.png"
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

    # Warn about missing expected columns but continue
    missing = [c for c in EXPECTED_COLUMNS if c not in columns]
    if missing:
        print(f"  WARNING: missing expected columns: {', '.join(missing)}")

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
