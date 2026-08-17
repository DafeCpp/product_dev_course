"""LOS-31: поведенческие регрессии на реальных golden-эпизодах."""

import csv
import math
import statistics
from pathlib import Path

import pytest

from simlib import (
    OversteerReplayConfig,
    find_invariant_violations,
    find_sim_host,
    load_telemetry_csv,
    run_batch,
)


RIDES = Path(__file__).parent / "fixtures" / "rides"
BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")


def _raw(name: str) -> list[dict[str, str]]:
    with (RIDES / name).open(newline="") as fh:
        return list(csv.DictReader(fh))


def _replay(name: str, **kwargs):
    path = RIDES / name
    frames = load_telemetry_csv(str(path))
    output = run_batch(frames, BIN, inverted_z_calib=True, **kwargs)
    assert len(output) == len(frames)
    violations = find_invariant_violations(output)
    assert not violations, violations[:5]
    return _raw(name), frames, output


def _f(row: dict[str, str], key: str) -> float:
    return float(row.get(key, "") or 0.0)


def _angle_delta(value: float, reference: float) -> float:
    return (value - reference + 180.0) % 360.0 - 180.0


def _circular_std_deg(values: list[float]) -> float:
    radians = [math.radians(value) for value in values]
    mean = math.degrees(
        math.atan2(
            statistics.mean(math.sin(value) for value in radians),
            statistics.mean(math.cos(value) for value in radians),
        )
    )
    return statistics.pstdev(_angle_delta(value, mean) for value in values)


def _unwrapped_span_deg(values: list[float]) -> float:
    unwrapped = [values[0]]
    for value in values[1:]:
        unwrapped.append(unwrapped[-1] + _angle_delta(value, unwrapped[-1]))
    return max(unwrapped) - min(unwrapped)


def _max_true_run(values) -> int:
    longest = current = 0
    for value in values:
        current = current + 1 if value else 0
        longest = max(longest, current)
    return longest


def test_static_tilt_keeps_pca_heading_stable():
    raw, _, output = _replay("golden_real_tilt_2026_08_02.csv")
    recorded_tilt = [
        max(abs(_f(row, "pitch_deg")), abs(_f(row, "roll_deg")))
        for row in raw
    ]
    assert 15.0 <= statistics.mean(recorded_tilt) <= 30.0
    # Первый HostStep публикует bootstrap snapshot с heading=0.
    assert _circular_std_deg([row["heading_deg"] for row in output[1:]]) < 1.0


def test_failsafe_episode_forces_neutral_pwm():
    _, frames, output = _replay("golden_real_failsafe_reconstructed.csv")
    assert all(not frame.rc_present and not frame.wifi_present for frame in frames)
    assert statistics.mean(frame.az for frame in frames) < -0.9
    assert all(row["failsafe"] == 1.0 for row in output)
    assert all(row["neutral"] == 1.0 for row in output)
    assert max(abs(row["throttle"]) for row in output) < 1.0e-6
    assert max(abs(row["steering"]) for row in output) < 1.0e-6
    # Калибровка перевёрнутого монтажа должна убрать ложный roll≈180°.
    assert max(abs(row["pitch_deg"]) for row in output[1:]) < 10.0
    assert max(abs(row["roll_deg"]) for row in output[1:]) < 10.0


def test_recorded_oversteer_episode_triggers_guard():
    raw, _, output = _replay(
        "golden_real_oversteer_2026_08_02.csv",
        drive_mode="drift",
        stabilize=True,
        oversteer=OversteerReplayConfig(
            slip_thresh_deg=10.0,
            rate_thresh_deg_s=30.0,
            throttle_reduction=0.7,
        ),
    )
    pre_phase = [row["replay_phase"] == "pre" for row in raw]
    active_phase = [row["replay_phase"] == "assert" for row in raw]
    assert any(pre_phase)
    assert any(active_phase)
    assert not any(
        out["oversteer_active"] > 0.5
        for marked, out in zip(pre_phase, output)
        if marked
    )
    assert _max_true_run(
        out["oversteer_active"] > 0.5
        for marked, out in zip(active_phase, output)
        if marked
    ) >= 3


def test_marked_straight_does_not_drift():
    raw, _, output = _replay("golden_real_straight_marker.csv")
    assert all(_f(row, "test_marker") == 1.0 for row in raw)
    # Первый HostStep публикует bootstrap snapshot с heading=0.
    assert _unwrapped_span_deg([row["heading_deg"] for row in output[1:]]) < 2.0


def test_reverse_without_input_does_not_inject_steering():
    raw, frames, output = _replay(
        "golden_real_reverse_2026_07_18.csv", stabilize=True
    )
    indices = [
        i for i, row in enumerate(raw) if row["replay_phase"] == "assert"
    ]
    assert len(indices) >= 100
    errors = [abs(output[i]["steering"] - frames[i].rc_steering) for i in indices]
    assert max(abs(output[i]["steering"]) for i in indices) < 0.15
    assert max(errors) < 0.1
    assert math.sqrt(statistics.mean(error * error for error in errors)) < 0.05
