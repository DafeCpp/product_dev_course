"""LOS-363: метрики испытаний по логу заезда (`tools/trial_metrics.py`).

Скрипт отлаживается без машины: логи SIL переводятся в формат
`TelemetryLogFrame` (CSV), метрики сверяются с независимым расчётом из
`test_testing_program_scenarios.py`; на синтетике проверяются точные значения.
"""

import csv
import math
import os
import sys

import pytest

from simlib import ClosedLoopSim, find_sim_host
from simlib.frame import SensorFrame
from simlib.sim_host_runner import run_batch

TOOLS = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "tools"))
sys.path.insert(0, TOOLS)

import trial_metrics as tm  # noqa: E402
from analyze_telemetry import load_csv  # noqa: E402

BIN = find_sim_host()
needs_sim = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")

DT_MS = 2
KIDS_DRIVE_MODE = 3


def _write_csv(path, rows: list[dict]):
    cols = list(rows[0])
    with open(path, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)
    return load_csv(str(path))[1]  # через тот же загрузчик, что и у CLI


def _sil_to_log(rows: list[dict], **extra) -> list[dict]:
    """Строки sim_host → колонки TelemetryLogFrame."""
    out = []
    for i, r in enumerate(rows):
        out.append({
            "ts_ms": (i + 1) * DT_MS,
            "ax": r["forward_accel"], "ay": 0.0, "az": 1.0,
            "gx": 0.0, "gy": 0.0, "gz": r["filtered_gz"],
            "speed_ms": r["ekf_speed_ms"],
            "throttle": r["throttle"], "steering": r["steering"],
            "yaw_rate_dps": math.degrees(r["ekf_yaw_rate"]),
            "pitch_deg": r["pitch_deg"], "roll_deg": r["roll_deg"],
            "rc_throttle": r["rc_throttle"], "rc_steering": r["rc_steering"],
            "cmd_throttle": r["cmd_throttle"], "cmd_steering": r["cmd_steering"],
            **{k: v(i, r) if callable(v) else v for k, v in extra.items()},
        })
    return out


# ── Yaw PID ──────────────────────────────────────────────────────────────

def _second_order_log(zeta: float, wn: float, steer: float, n=1500):
    """Синтетический шаг руля: yaw rate — звено 2-го порядка к уставке."""
    setpoint = tm.NORMAL_STEER_TO_YAW_DPS * steer
    dt = DT_MS / 1000.0
    step_at = 200  # отсчёт, где команда меняется
    y = v = 0.0
    rows = []
    for i in range(n):
        u = setpoint if i >= step_at else 0.0
        a = wn * wn * (u - y) - 2 * zeta * wn * v
        v += a * dt
        y += v * dt
        rows.append({"ts_ms": (i + 1) * DT_MS, "rc_steering": steer if i >= step_at else 0.0,
                     "yaw_rate_dps": y})
    return rows, step_at, setpoint


def test_yaw_step_overshoot_matches_second_order_theory(tmp_path):
    zeta = 0.5
    rows, step_at, setpoint = _second_order_log(zeta, wn=30.0, steer=0.4)
    log = _write_csv(tmp_path / "yaw.csv", rows)
    steps = tm.find_steering_steps(log)
    assert len(steps) == 1 and abs(steps[0] - (step_at - 1)) <= 1
    r = tm.yaw_step_response(log, steps[0])
    theory = math.exp(-math.pi * zeta / math.sqrt(1 - zeta ** 2))  # ≈ 16.3 %
    assert r["overshoot"] == pytest.approx(theory, abs=0.01)
    assert r["setpoint_dps"] == pytest.approx(36.0)
    # Огибающая ±10 %: t ≈ -ln(0.1·sqrt(1-ζ²))/(ζ·wn), с запасом на численную схему.
    envelope_ms = 1000 * -math.log(0.1 * math.sqrt(1 - zeta ** 2)) / (zeta * 30.0)
    assert 0 < r["settling_ms"] <= envelope_ms + 20


def test_yaw_step_never_settling_reports_inf(tmp_path):
    rows, step_at, _ = _second_order_log(0.02, wn=60.0, steer=0.4, n=700)
    log = _write_csv(tmp_path / "ring.csv", rows)
    r = tm.yaw_step_response(log, tm.find_steering_steps(log)[0], window_ms=800)
    assert r["overshoot"] > 0.5 and r["settling_ms"] > tm.YAW_SETTLING_LIMIT_MS
    res = tm.check_yaw_step(log)
    assert res and res[0].passed is False


def test_yaw_step_passes_for_well_damped_loop(tmp_path):
    rows, _, _ = _second_order_log(0.9, wn=60.0, steer=0.4)
    res = tm.check_yaw_step(_write_csv(tmp_path / "ok.csv", rows))
    assert [r.passed for r in res] == [True]


def test_yaw_step_negative_steer_is_mirrored(tmp_path):
    rows, _, _ = _second_order_log(0.5, wn=30.0, steer=0.4)
    for r in rows:
        r["rc_steering"] *= -1
        r["yaw_rate_dps"] *= -1
    log = _write_csv(tmp_path / "neg.csv", rows)
    r = tm.yaw_step_response(log, tm.find_steering_steps(log)[0])
    assert r["overshoot"] == pytest.approx(0.163, abs=0.01)


def test_yaw_step_missing_step(tmp_path):
    rows = [{"ts_ms": i * 10, "rc_steering": 0.0, "yaw_rate_dps": 0.0} for i in range(300)]
    res = tm.check_yaw_step(_write_csv(tmp_path / "flat.csv", rows))
    assert res[0].passed is False and "НЕ НАЙДЕН" in res[0].value


@needs_sim
def test_yaw_step_on_sil_log_matches_scenario_test(tmp_path):
    """На логе SIL метрика совпадает с расчётом из test_testing_program_scenarios."""
    steer = 0.4
    with ClosedLoopSim(BIN, dynamic=True, stabilize=True, sensor_noise=False) as sim:
        sim.run(1500, rc_throttle=0.4, rc_steering=0.0)
        rows = sim.run(1000, rc_throttle=0.4, rc_steering=steer)
    log = _write_csv(tmp_path / "sil.csv", _sil_to_log(rows))
    steps = tm.find_steering_steps(log)
    assert len(steps) == 1
    r = tm.yaw_step_response(log, steps[0], window_ms=1000)

    # Независимый расчёт по тому же окну: [t0, t0 + 1000 мс].
    setpoint = tm.NORMAL_STEER_TO_YAW_DPS * steer
    t0 = r["t0_ms"]
    win = [(row["ts_ms"] - t0, row["yaw_rate_dps"]) for row in log
           if t0 <= row["ts_ms"] <= t0 + 1000]
    assert r["setpoint_dps"] == pytest.approx(setpoint)
    assert r["overshoot"] == pytest.approx((max(y for _, y in win) - setpoint) / setpoint)
    out = [t for t, y in win if abs(y - setpoint) > 0.1 * setpoint]
    # SIL держит автоколебания (LOS-366): в полосу ±10 % так и не входит.
    assert out and r["settling_ms"] >= out[-1]


# ── Дрейф bias гироскопа ─────────────────────────────────────────────────

def _rest_log(minutes: float, gz_slope_dps_min=0.0, pitch_slope=0.0, hz=100):
    n = int(minutes * 60 * hz)
    rows = []
    for i in range(n):
        t_min = i / hz / 60.0
        rows.append({
            "ts_ms": i * 1000 // hz, "throttle": 0.0, "speed_ms": 0.0,
            "gx": 0.1, "gy": -0.2, "gz": 0.3 + gz_slope_dps_min * t_min,
            "pitch_deg": 1.0 + pitch_slope * t_min, "roll_deg": 0.0,
        })
    return rows


def test_gyro_bias_drift_recovers_known_slope(tmp_path):
    log = _write_csv(tmp_path / "rest.csv", _rest_log(5, gz_slope_dps_min=0.2, pitch_slope=0.5))
    d = tm.stationary_drift(log)
    assert d["duration_s"] == pytest.approx(300, abs=1)
    assert d["gz"]["slope_per_min"] == pytest.approx(0.2, abs=1e-3)
    assert d["gx"]["slope_per_min"] == pytest.approx(0.0, abs=1e-6)
    assert d["gx"]["mean"] == pytest.approx(0.1)
    assert d["pitch_deg"]["slope_per_min"] == pytest.approx(0.5, abs=1e-3)
    # уход за запись: разность средних последней и первой пятой частей = 4 мин × наклон
    assert d["gz"]["span"] == pytest.approx(0.2 * 4, abs=0.01)
    assert all(r.passed for r in tm.check_gyro_bias(log))


def test_gyro_bias_flags_fast_drift(tmp_path):
    log = _write_csv(tmp_path / "bad.csv", _rest_log(3, gz_slope_dps_min=2.0))
    res = {r.name: r for r in tm.check_gyro_bias(log)}
    assert res["Дрейф в покое: bias gz"].passed is False
    assert res["Дрейф в покое: bias gx"].passed is True


def test_gyro_bias_ignores_driving(tmp_path):
    rows = _rest_log(1)
    for r in rows[:3000]:  # первые 30 с едем, gz там не bias
        r["throttle"] = 0.5
        r["gz"] = 50.0
    d = tm.stationary_drift(_write_csv(tmp_path / "mix.csv", rows))
    assert d["gz"]["mean"] == pytest.approx(0.3, abs=1e-6)


def test_gyro_bias_without_rest(tmp_path):
    rows = _rest_log(0.5)
    for r in rows:
        r["throttle"] = 0.5
    res = tm.check_gyro_bias(_write_csv(tmp_path / "drive.csv", rows))
    assert res[0].passed is False and "МАЛО" in res[0].value


@needs_sim
def test_gyro_bias_on_sil_rest_log(tmp_path):
    with ClosedLoopSim(BIN, sensor_noise=True) as sim:
        rows = sim.run(5000, rc_throttle=0.0, rc_steering=0.0)  # 10 с покоя
    log = _write_csv(tmp_path / "sil_rest.csv", _sil_to_log(rows))
    d = tm.stationary_drift(log)
    assert d["samples"] > 4000
    assert abs(d["gz"]["mean"]) < 1.0  # в покое гироскоп SIL около нуля


# ── Failsafe ─────────────────────────────────────────────────────────────

def _pulses(ms: int, thr_us: int, str_us: int) -> list[SensorFrame]:
    return [SensorFrame(dt_ms=DT_MS, rc_pulse_us=(thr_us, str_us))
            for _ in range(ms // DT_MS)]


@needs_sim
def test_failsafe_latency_on_sil_rc_loss(tmp_path):
    """RC пропал на 2000 мс: флаг rc_ok падает после таймаута декодера, нейтраль ≤250 мс всего."""
    loss_at = 2000
    frames = _pulses(loss_at, 1600, 1600) + _pulses(800, 0, 0) + _pulses(1000, 1600, 1600)
    rows = run_batch(frames, BIN)
    flag_drop = loss_at + int(tm.RC_SOURCE_TIMEOUT_MS)

    def rc_ok(i, _r):
        t = (i + 1) * DT_MS
        return 0 if flag_drop <= t < loss_at + 800 else 1

    log = _write_csv(tmp_path / "rc_loss.csv", _sil_to_log(rows, rc_ok=rc_ok, wifi_ok=0))
    events = tm.failsafe_latencies(log)
    assert len(events) == 1 and events[0]["was_moving"]
    e = events[0]
    assert e["t_ms"] == pytest.approx(flag_drop, abs=2 * DT_MS)
    total = e["from_flag_ms"] + e["source_timeout_ms"]
    assert 180 <= total <= 250
    res = tm.check_failsafe(log)
    assert [r.passed for r in res] == [True]


def test_failsafe_slow_neutral_fails(tmp_path):
    rows = []
    for i in range(400):
        t = i * 10
        lost = t >= 1000
        rows.append({"ts_ms": t, "rc_ok": 0 if lost else 1, "wifi_ok": 0,
                     "throttle": 0.0 if t >= 1300 else 0.4, "steering": 0.0})
    res = tm.check_failsafe(_write_csv(tmp_path / "slow.csv", rows))
    assert res[0].passed is False  # 300 мс от флага + 90 мс таймаута


def test_failsafe_no_loss_event(tmp_path):
    rows = [{"ts_ms": i * 10, "rc_ok": 1, "wifi_ok": 0, "throttle": 0.3, "steering": 0.0}
            for i in range(100)]
    res = tm.check_failsafe(_write_csv(tmp_path / "ok.csv", rows))
    assert res[0].passed is False and "НЕ НАЙДЕНО" in res[0].value


def test_failsafe_wifi_loss_uses_wifi_timeout(tmp_path):
    rows = []
    for i in range(300):
        t = i * 10
        lost = t >= 1000
        rows.append({"ts_ms": t, "rc_ok": 0, "wifi_ok": 0 if lost else 1,
                     "throttle": 0.0 if t >= 1100 else 0.3, "steering": 0.0})
    e = tm.failsafe_latencies(_write_csv(tmp_path / "wifi.csv", rows))[0]
    assert e["source_timeout_ms"] == tm.WIFI_SOURCE_TIMEOUT_MS
    assert e["from_flag_ms"] == pytest.approx(100, abs=10)


# ── Kids Mode ────────────────────────────────────────────────────────────

@needs_sim
def test_kids_peaks_match_sil_rows(tmp_path):
    with ClosedLoopSim(BIN, drive_mode="kids", sensor_noise=False) as sim:
        rows = sim.run(2000, rc_throttle=1.0, rc_steering=0.0)
    log = _write_csv(tmp_path / "kids.csv", _sil_to_log(rows, drive_mode=KIDS_DRIVE_MODE))
    p = tm.kids_peaks(log)
    assert p["max_speed_ms"] == pytest.approx(max(abs(r["ekf_speed_ms"]) for r in rows), rel=1e-4)
    assert p["max_accel_ms2"] == pytest.approx(
        max(abs(r["forward_accel"]) for r in rows) * tm.G, rel=1e-4)
    res = tm.check_kids(log, max_speed_ms=p["max_speed_ms"] + 0.01,
                        max_accel_ms2=p["max_accel_ms2"] - 0.5)
    assert [r.passed for r in res] == [True, False]


def test_kids_selects_only_kids_samples(tmp_path):
    rows = [{"ts_ms": i * 10, "speed_ms": 1.0 if i < 50 else 9.0, "ax": 0.1,
             "drive_mode": 3 if i < 50 else 0} for i in range(100)]
    p = tm.kids_peaks(_write_csv(tmp_path / "mix.csv", rows))
    assert p["max_speed_ms"] == pytest.approx(1.0) and p["samples"] == 50


def test_kids_without_samples(tmp_path):
    rows = [{"ts_ms": i * 10, "speed_ms": 1.0, "ax": 0.1, "drive_mode": 0} for i in range(20)]
    res = tm.check_kids(_write_csv(tmp_path / "none.csv", rows))
    assert res[0].passed is False


def test_kids_no_limits_is_informational(tmp_path):
    rows = [{"ts_ms": i * 10, "speed_ms": 1.0, "ax": 0.1} for i in range(20)]
    res = tm.check_kids(_write_csv(tmp_path / "info.csv", rows))
    assert [r.passed for r in res] == [None, None]


# ── EKF против дистанции ─────────────────────────────────────────────────

def test_ekf_distance_error(tmp_path):
    # 2 м/с ровно 2.5 с = 5 м
    rows = [{"ts_ms": i * 10, "speed_ms": 2.0 if 100 <= i <= 350 else 0.0}
            for i in range(500)]
    log = _write_csv(tmp_path / "dist.csv", rows)
    assert tm.integrate_distance(log) == pytest.approx(5.0, abs=0.05)
    assert tm.check_ekf_distance(log, 5.0)[0].passed is True
    bad = tm.check_ekf_distance(log, 4.5)[0]
    assert bad.passed is False and "+11" in bad.value


def test_ekf_distance_window(tmp_path):
    rows = [{"ts_ms": i * 10, "speed_ms": 1.0} for i in range(300)]  # 3 с
    log = _write_csv(tmp_path / "w.csv", rows)
    assert tm.integrate_distance(log, 1000, 2000) == pytest.approx(1.0, abs=0.02)


# ── CLI ──────────────────────────────────────────────────────────────────

def test_cli_exit_codes(tmp_path, capsys):
    rows = _rest_log(0.5)
    path = tmp_path / "rest.csv"
    _write_csv(path, rows)
    assert tm.main([str(path), "--only", "gyro-bias"]) == 0
    assert "[PASS]" in capsys.readouterr().out

    path2 = tmp_path / "rest_bad.csv"
    _write_csv(path2, _rest_log(3, gz_slope_dps_min=2.0))
    assert tm.main([str(path2), "--only", "gyro-bias"]) == 2

    assert tm.main([str(tmp_path / "missing.csv")]) == 1


def test_cli_skips_ekf_dist_without_distance(tmp_path, capsys):
    path = tmp_path / "rest.csv"
    _write_csv(path, _rest_log(0.5))
    tm.main([str(path), "--only", "ekf-dist"])
    assert "EKF: дистанция" not in capsys.readouterr().out


def test_cli_skips_metrics_without_data_unless_requested(tmp_path, capsys):
    """Лог покоя: без --only остальные метрики пропускаются, с --only — провал."""
    path = tmp_path / "rest.csv"
    _write_csv(path, _rest_log(0.5))
    assert tm.main([str(path)]) == 0
    out = capsys.readouterr().out
    assert "[SKIP] Yaw PID step" in out and "[SKIP] Failsafe" in out
    assert "[PASS] Дрейф в покое: bias gz" in out
    assert tm.main([str(path), "--only", "yaw-step"]) == 2


def test_cli_without_any_data_is_an_error(tmp_path):
    path = tmp_path / "empty.csv"
    _write_csv(path, [{"ts_ms": i * 10, "throttle": 0.5} for i in range(50)])
    assert tm.main([str(path)]) == 2
