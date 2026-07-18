import math
import os

import numpy as np
import pytest

import simlib.validation as val
from simlib import SimParams, channel_metrics, fit_params, fit_params_multi, simulate
from simlib.validation import CHANNELS, load_drive_log


def _synthetic_drive(params: SimParams, n: int = 400, dt: float = 0.01) -> dict:
    """«Записанная» поездка = выход модели при known params (round-trip)."""
    i = np.arange(n)
    throttle = 0.45 + 0.1 * np.sin(i * 0.02)
    steering = 0.5 * np.sin(i * 0.04)
    dts = np.full(n, dt)
    rec = simulate(params, throttle, steering, dts)
    return {"dt": dts, "throttle": throttle, "steering": steering, "rec": rec}


def test_metrics_identical_data_zero_rmse():
    p = SimParams()
    d = _synthetic_drive(p)
    pred = simulate(p, d["throttle"], d["steering"], d["dt"])
    m = channel_metrics(pred, d["rec"])
    for ch in CHANNELS:
        assert m[ch]["rmse"] < 1e-9


def test_fit_reduces_cost():
    true = SimParams(max_accel=8.0)
    d = _synthetic_drive(true)
    base = SimParams(max_accel=4.0)
    pred0 = simulate(base, d["throttle"], d["steering"], d["dt"])
    c0 = val._cost(pred0, d["rec"], CHANNELS)
    fitted, _ = fit_params(d, base, ["max_accel"])
    pred1 = simulate(fitted, d["throttle"], d["steering"], d["dt"])
    c1 = val._cost(pred1, d["rec"], CHANNELS)
    assert c1 < 0.1 * c0


def test_fit_recovers_known_params():
    true = SimParams(max_accel=8.0, servo_max_deg=25.0)
    d = _synthetic_drive(true)
    base = SimParams(max_accel=5.0, servo_max_deg=18.0)
    fitted, _ = fit_params(d, base, ["max_accel", "servo_max_deg"])
    assert math.isclose(fitted.max_accel, 8.0, rel_tol=0.05)
    assert math.isclose(fitted.servo_max_deg, 25.0, rel_tol=0.05)


def test_fit_multi_recovers_known_params():
    true = SimParams(max_accel=8.0, drag_coeff=0.8)
    d1 = _synthetic_drive(true)
    d2 = _synthetic_drive(true, n=250, dt=0.02)
    base = SimParams(max_accel=5.0, drag_coeff=0.4)
    fitted, _ = fit_params_multi([d1, d2], base, ["max_accel", "drag_coeff"])
    assert math.isclose(fitted.max_accel, 8.0, rel_tol=0.05)
    assert math.isclose(fitted.drag_coeff, 0.8, rel_tol=0.05)


def test_fit_keeps_params_positive():
    true = SimParams(drag_coeff=0.8)
    d = _synthetic_drive(true)
    base = SimParams(drag_coeff=0.3)
    fitted, _ = fit_params(d, base, ["drag_coeff", "max_accel"])
    assert fitted.drag_coeff > 0.0
    assert fitted.max_accel > 0.0


def test_plot_channels_writes_file(tmp_path):
    pytest.importorskip("matplotlib")
    p = SimParams()
    d = _synthetic_drive(p, n=50)
    pred = simulate(p, d["throttle"], d["steering"], d["dt"])
    out = tmp_path / "val.png"
    val.plot_channels(pred, d["rec"], d["dt"], str(out))
    assert out.exists() and out.stat().st_size > 0


@pytest.mark.skipif(not os.environ.get("SIM_VALIDATION_LOG"),
                    reason="SIM_VALIDATION_LOG не задан (реальный лог)")
def test_real_log_metrics_finite():
    drive = load_drive_log(os.environ["SIM_VALIDATION_LOG"])
    fitted, _ = fit_params(drive, SimParams(),
                           ["max_accel", "servo_max_deg", "drag_coeff"])
    pred = simulate(fitted, drive["throttle"], drive["steering"], drive["dt"])
    m = channel_metrics(pred, drive["rec"])
    for ch in CHANNELS:
        assert math.isfinite(m[ch]["rmse"])
