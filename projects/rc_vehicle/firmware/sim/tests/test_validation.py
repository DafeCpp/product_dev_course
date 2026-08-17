import math
import os

import numpy as np
import pytest

import simlib.validation as val
from simlib import (
    RoadNoiseModel,
    SimParams,
    channel_metrics,
    fit_params,
    fit_params_multi,
    fit_road_noise_params,
    simulate,
)


def _synthetic_drive(params: SimParams, n: int = 400, dt: float = 0.01,
                     dynamic: bool = False) -> dict:
    """«Записанная» поездка = выход модели при known params (round-trip)."""
    i = np.arange(n)
    throttle = 0.45 + 0.1 * np.sin(i * 0.02)
    steering = 0.5 * np.sin(i * 0.04)
    dts = np.full(n, dt)
    rec = simulate(params, throttle, steering, dts, dynamic=dynamic)
    return {"dt": dts, "throttle": throttle, "steering": steering, "rec": rec}


def test_metrics_identical_data_zero_rmse():
    p = SimParams()
    d = _synthetic_drive(p)
    pred = simulate(p, d["throttle"], d["steering"], d["dt"])
    m = channel_metrics(pred, d["rec"])
    for ch in val.CHANNELS:
        assert m[ch]["rmse"] < 1e-9


def test_fit_reduces_cost():
    true = SimParams(max_accel=8.0)
    d = _synthetic_drive(true)
    base = SimParams(max_accel=4.0)
    pred0 = simulate(base, d["throttle"], d["steering"], d["dt"])
    c0 = val._cost(pred0, d["rec"], val.CHANNELS)
    fitted, _ = fit_params(d, base, ["max_accel"])
    pred1 = simulate(fitted, d["throttle"], d["steering"], d["dt"])
    c1 = val._cost(pred1, d["rec"], val.CHANNELS)
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


def test_dynamic_fit_recovers_cornering_params():
    true = SimParams(Caf=45.0, Car=35.0, Iz=0.12)
    d = _synthetic_drive(true, n=600, dynamic=True)
    base = SimParams(Caf=60.0, Car=60.0, Iz=0.05)
    fitted, _ = fit_params_multi(
        [d], base, ["Caf", "Car", "Iz"],
        channels=("yaw_rate_dps",), dynamic=True)
    assert fitted.Caf == pytest.approx(true.Caf, rel=0.08)
    assert fitted.Car == pytest.approx(true.Car, rel=0.08)
    assert fitted.Iz == pytest.approx(true.Iz, rel=0.08)


def test_simulate_exposes_dynamic_slip():
    p = SimParams()
    d = _synthetic_drive(p, n=200, dynamic=True)
    assert "slip_deg" in d["rec"]
    assert np.all(np.isfinite(d["rec"]["slip_deg"]))
    assert np.max(np.abs(d["rec"]["slip_deg"])) > 0.0


def test_legacy_yaw_sign_detection_unwraps_heading():
    ts = np.arange(200, dtype=float) * 10.0
    current_rate = 50.0 + 20.0 * np.sin(np.arange(200) * 0.08)
    unwrapped = 170.0 + np.cumsum(current_rate * 0.01)
    yaw_deg = ((unwrapped + 180.0) % 360.0) - 180.0
    legacy_rate = -current_rate
    fixed, _ = val._maybe_fix_legacy_signs(
        ts, legacy_rate, yaw_deg, np.ones(200), np.zeros(200))
    assert fixed == pytest.approx(current_rate)


def test_current_yaw_sign_detection_leaves_sign_unchanged():
    ts = np.arange(200, dtype=float) * 10.0
    current_rate = 50.0 + 20.0 * np.sin(np.arange(200) * 0.08)
    unwrapped = 170.0 + np.cumsum(current_rate * 0.01)
    yaw_deg = ((unwrapped + 180.0) % 360.0) - 180.0
    fixed, _ = val._maybe_fix_legacy_signs(
        ts, current_rate, yaw_deg, np.ones(200), np.zeros(200))
    assert fixed == pytest.approx(current_rate)


def test_load_drive_log_exposes_recorded_slip(tmp_path):
    path = tmp_path / "drive.csv"
    path.write_text(
        "ts_ms,throttle,steering,yaw_rate_dps,speed_ms,slip_deg,ax\n"
        "0,0.2,0.1,5.0,1.0,3.5,0.0\n"
        "10,0.2,0.1,5.0,1.1,4.5,0.0\n",
        encoding="utf-8")
    drive = val.load_drive_log(path)
    assert drive["rec"]["slip_deg"] == pytest.approx([3.5, 4.5])


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
    drive = val.load_drive_log(os.environ["SIM_VALIDATION_LOG"])
    fitted, _ = fit_params(drive, SimParams(),
                           ["max_accel", "servo_max_deg", "drag_coeff"])
    pred = simulate(fitted, drive["throttle"], drive["steering"], drive["dt"])
    m = channel_metrics(pred, drive["rec"])
    for ch in val.CHANNELS:
        assert math.isfinite(m[ch]["rmse"])


def test_fit_road_noise_recovers_synthetic_sigma_and_spectrum():
    true = SimParams(
        road_ax_sigma_0_g=0.02, road_ax_sigma_per_ms=0.08,
        road_ay_sigma_0_g=0.03, road_ay_sigma_per_ms=0.12,
        road_az_sigma_0_g=0.04, road_az_sigma_per_ms=0.15,
        road_roll_rate_sigma_0_dps=5.0,
        road_roll_rate_sigma_per_ms=9.0,
        road_pitch_rate_sigma_0_dps=4.0,
        road_pitch_rate_sigma_per_ms=7.0,
        road_roll_frequency_hz=11.0,
        road_pitch_frequency_hz=7.0,
        road_attitude_damping=0.3,
    )
    noise = RoadNoiseModel(true, seed=31)
    speed = np.repeat([0.0, 1.0, 2.5], 5000)
    samples = [noise.sample(float(v), 10) for v in speed]
    residual = {
        "ax": np.asarray([s.accel_g[0] for s in samples]),
        "ay": np.asarray([s.accel_g[1] for s in samples]),
        "az": np.asarray([s.accel_g[2] for s in samples]),
        "gx": np.asarray([s.gyro_dps[0] for s in samples]),
        "gy": np.asarray([s.gyro_dps[1] for s in samples]),
    }
    fitted, diagnostics = fit_road_noise_params(
        [{"path": "synthetic", "speed_ms": speed,
          "residual": residual, "dt_s": 0.01}],
        SimParams(), min_samples=500)

    assert fitted.road_ax_sigma_0_g == pytest.approx(
        true.road_ax_sigma_0_g, rel=0.12)
    assert fitted.road_ax_sigma_per_ms == pytest.approx(
        true.road_ax_sigma_per_ms, rel=0.08)
    assert fitted.road_roll_frequency_hz == pytest.approx(
        true.road_roll_frequency_hz, abs=1.0)
    assert fitted.road_pitch_frequency_hz == pytest.approx(
        true.road_pitch_frequency_hz, abs=1.0)
    assert diagnostics["channels"]["ax"]["bins"] == 3
