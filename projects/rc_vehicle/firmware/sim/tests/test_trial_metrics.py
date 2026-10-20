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
    rows = [{"ts_ms": i * 10, "speed_ms": 1.0, "ax": 0.1, "drive_mode": 3}
            for i in range(20)]
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
    rows = _rest_log(1.5)
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
    _write_csv(path, _rest_log(1.5))
    assert tm.main([str(path), "--only", "ekf-dist"]) == 2
    assert "EKF: дистанция" not in capsys.readouterr().out


def test_cli_skips_metrics_without_data_unless_requested(tmp_path, capsys):
    """Лог покоя: без --only остальные метрики пропускаются, с --only — провал."""
    path = tmp_path / "rest.csv"
    _write_csv(path, _rest_log(1.5))
    assert tm.main([str(path)]) == 0
    out = capsys.readouterr().out
    assert "[SKIP] Yaw PID step" in out and "[SKIP] Failsafe" in out
    assert "[PASS] Дрейф в покое: bias gz" in out
    assert tm.main([str(path), "--only", "yaw-step"]) == 2


def test_cli_without_any_data_is_an_error(tmp_path):
    path = tmp_path / "empty.csv"
    _write_csv(path, [{"ts_ms": i * 10, "throttle": 0.5} for i in range(50)])
    assert tm.main([str(path)]) == 2


# ── Замечания ревью (PR #358) ────────────────────────────────────────────

def _two_steps_log(second: float, n=900):
    """Шаг руля 0 → 0.4 на 100-м отсчёте, затем `second` на 400-м (10 мс/отсчёт)."""
    rows = []
    for i in range(n):
        cmd = 0.0 if i < 100 else (0.4 if i < 400 else second)
        rows.append({"ts_ms": i * 10, "rc_steering": cmd,
                     "yaw_rate_dps": 36.0 if 102 <= i < 400 else 0.0})
    return rows


def test_yaw_step_return_to_zero_is_not_a_step(tmp_path):
    """Руль 0 → 0.4 → 0 (и потеря RC обнуляет rc_steering): второй «шаг» не считается и не роняет скрипт."""
    log = _write_csv(tmp_path / "updown.csv", _two_steps_log(0.0))
    assert len(tm.find_steering_steps(log)) == 1
    res = tm.check_yaw_step(log)
    assert len(res) == 1 and res[0].passed is True


def test_yaw_step_window_ends_at_next_step(tmp_path):
    """Второй шаг 0.4 → -0.4 через 3 с: переходный процесс первого не захватывает второй."""
    rows = _two_steps_log(-0.4)
    for r in rows[400:]:
        r["yaw_rate_dps"] = -36.0
    log = _write_csv(tmp_path / "two.csv", rows)
    res = tm.check_yaw_step(log)
    assert len(res) == 2 and all(r.passed for r in res)


def test_yaw_step_zero_setpoint_does_not_raise(tmp_path):
    rows = [{"ts_ms": i * 10, "rc_steering": 0.0, "yaw_rate_dps": 0.0} for i in range(50)]
    log = _write_csv(tmp_path / "z.csv", rows)
    with pytest.raises(ValueError):
        tm.yaw_step_response(log, 10)
    assert tm.run_metrics(log)[0].no_data  # без --only это SKIP, не падение


def test_yaw_step_wifi_log_is_reported_as_unsupported(tmp_path):
    """Прошивка пишет rc_steering только для RC: у Wi-Fi-заезда там нули, а не молчаливый PASS."""
    rows = [{"ts_ms": i * 10, "rc_steering": 0.0, "cmd_steering": 0.4 if i > 50 else 0.0,
             "yaw_rate_dps": 36.0} for i in range(200)]
    res = tm.check_yaw_step(_write_csv(tmp_path / "wifi.csv", rows))
    assert res[0].no_data and "RC" in res[0].value


def test_yaw_step_overshoot_and_settling_checked_separately(tmp_path):
    """Каждый критерий в отдельности: большой overshoot при быстром входе в полосу и наоборот."""
    def log(profile):
        rows = [{"ts_ms": i * 10, "rc_steering": 0.0 if i < 50 else 0.4,
                 "yaw_rate_dps": profile(i - 50) if i >= 50 else 0.0} for i in range(300)]
        return _write_csv(tmp_path / f"p{id(profile)}.csv", rows)

    sp = 36.0
    # Пик 30 % на одном отсчёте, дальше в полосе: settling мал, overshoot нет.
    peak = log(lambda k: sp if k != 5 else sp * 1.3)
    r = tm.check_yaw_step(peak)[0]
    assert r.passed is False and "overshoot" in r.value
    assert r.value.count("НЕ ВЫПОЛНЕНО") == 1
    # Медленный подход без перелёта: overshoot 0, settling 400 мс.
    slow = log(lambda k: sp * min(1.0, k / 40.0))
    r = tm.check_yaw_step(slow)[0]
    assert r.passed is False and r.value.count("НЕ ВЫПОЛНЕНО") == 1
    res = tm.yaw_step_response(slow and _write_csv(tmp_path / "s.csv", [
        {"ts_ms": i * 10, "rc_steering": 0.0 if i < 50 else 0.4,
         "yaw_rate_dps": sp * min(1.0, max(0, i - 50) / 40.0)} for i in range(300)]), 49)
    assert res["overshoot"] < 0.01 and 340 <= res["settling_ms"] <= 380


def test_yaw_step_skips_non_normal_modes_without_gain(tmp_path):
    rows = _two_steps_log(0.0)
    for r in rows:
        r["drive_mode"] = 1
    log = _write_csv(tmp_path / "sport.csv", rows)
    assert tm.check_yaw_step(log)[0].no_data
    assert tm.check_yaw_step(log, gain_dps=90.0)[0].passed is True


def test_kids_info_only_exits_zero(tmp_path, capsys):
    rows = [{"ts_ms": i * 10, "speed_ms": 1.0, "ax": 0.1, "drive_mode": 3} for i in range(20)]
    path = tmp_path / "kids.csv"
    _write_csv(path, rows)
    assert tm.main([str(path), "--only", "kids"]) == 0
    assert "критерии не заданы" in capsys.readouterr().out


def test_kids_uses_absolute_values(tmp_path):
    rows = [{"ts_ms": i * 10, "speed_ms": -2.0, "ax": -0.5, "drive_mode": 3} for i in range(20)]
    p = tm.kids_peaks(_write_csv(tmp_path / "rev.csv", rows))
    assert p["max_speed_ms"] == pytest.approx(2.0)
    assert p["max_accel_ms2"] == pytest.approx(0.5 * tm.G)


def test_kids_requires_drive_mode_column(tmp_path):
    rows = [{"ts_ms": i * 10, "speed_ms": 1.0, "ax": 0.1} for i in range(20)]
    res = tm.check_kids(_write_csv(tmp_path / "nomode.csv", rows))
    assert res[0].no_data and "drive_mode" in res[0].value


def test_gyro_bias_requires_long_rest(tmp_path):
    log = _write_csv(tmp_path / "short.csv", _rest_log(0.5))  # 30 с
    res = tm.check_gyro_bias(log)
    assert res[0].no_data and "нужно" in res[0].value
    assert all(r.passed for r in tm.check_gyro_bias(log, min_rest_s=10))


def test_gyro_bias_angle_threshold(tmp_path):
    log = _write_csv(tmp_path / "pitch.csv", _rest_log(3, pitch_slope=3.0))
    res = {r.name: r for r in tm.check_gyro_bias(log)}
    assert res["Дрейф в покое: pitch_deg"].passed is False
    assert res["Дрейф в покое: roll_deg"].passed is True


def test_failsafe_total_includes_source_timeout(tmp_path):
    """Нейтраль через 200 мс после падения флага RC: с таймаутом источника 290 мс, это провал."""
    rows = []
    for i in range(300):
        t = i * 10
        rows.append({"ts_ms": t, "rc_ok": 0 if t >= 1000 else 1, "wifi_ok": 0,
                     "throttle": 0.0 if t >= 1200 else 0.4, "steering": 0.0})
    res = tm.check_failsafe(_write_csv(tmp_path / "f.csv", rows))
    assert res[0].passed is False and "290" in res[0].value


def test_failsafe_flags_standing_car(tmp_path):
    rows = [{"ts_ms": i * 10, "rc_ok": 0 if i >= 100 else 1, "wifi_ok": 0,
             "throttle": 0.0, "steering": 0.0} for i in range(200)]
    res = tm.check_failsafe(_write_csv(tmp_path / "stand.csv", rows))
    assert "уже стояла" in res[0].value


def test_failsafe_neutral_after_source_returned_does_not_count(tmp_path):
    rows = []
    for i in range(300):
        t = i * 10
        back = t >= 1500
        rows.append({"ts_ms": t, "rc_ok": 1 if (back or t < 1000) else 0, "wifi_ok": 0,
                     "throttle": 0.0 if t >= 2000 else 0.4, "steering": 0.0})
    res = tm.check_failsafe(_write_csv(tmp_path / "back.csv", rows))
    assert res[0].passed is False and "не наступила" in res[0].value


def test_failsafe_takes_larger_timeout_when_both_sources_were_alive(tmp_path):
    rows = [{"ts_ms": i * 10, "rc_ok": 0 if i >= 100 else 1,
             "wifi_ok": 0 if i >= 100 else 1,
             "throttle": 0.0 if i >= 110 else 0.4, "steering": 0.0} for i in range(200)]
    e = tm.failsafe_latencies(_write_csv(tmp_path / "both.csv", rows))[0]
    assert e["source_timeout_ms"] == tm.WIFI_SOURCE_TIMEOUT_MS


def test_ekf_distance_rejects_nonpositive(tmp_path, capsys):
    log = _write_csv(tmp_path / "d.csv", [{"ts_ms": i * 10, "speed_ms": 1.0} for i in range(50)])
    with pytest.raises(ValueError):
        tm.check_ekf_distance(log, 0.0)
    assert tm.main([str(tmp_path / "d.csv"), "--only", "ekf-dist", "--distance-m", "0"]) == 1


def test_ekf_distance_ignores_backwards_timestamps(tmp_path):
    rows = [{"ts_ms": t, "speed_ms": 1.0} for t in (0, 1000, 500, 2000)]
    assert tm.integrate_distance(_write_csv(tmp_path / "back.csv", rows)) == pytest.approx(2.5)


def test_integrate_distance_uses_absolute_speed(tmp_path):
    rows = [{"ts_ms": i * 100, "speed_ms": -1.0} for i in range(11)]
    assert tm.integrate_distance(_write_csv(tmp_path / "rev.csv", rows)) == pytest.approx(1.0)


def test_yaw_step_setpoint_is_robust_to_command_glitch(tmp_path):
    """Выброс команды в конце окна (дребезг пульта) не должен менять уставку."""
    rows = _two_steps_log(0.0, n=140)
    rows[-1]["rc_steering"] = 1.0
    log = _write_csv(tmp_path / "glitch.csv", rows)
    r = tm.yaw_step_response(log, 99, window_ms=400)
    assert r["setpoint_dps"] == pytest.approx(36.0)


def _pulse_steer_log(hold_ms: int, n=300):
    """Руль 0 → 0.4 на 50-м отсчёте, удержание hold_ms, потом возврат к нулю."""
    rows = []
    release = 50 + hold_ms // 10
    for i in range(n):
        held = 50 <= i < release
        rows.append({"ts_ms": i * 10, "rc_steering": 0.4 if held else 0.0,
                     "yaw_rate_dps": 36.0 if 52 <= i < release else 0.0})
    return rows


def test_yaw_step_released_before_window_end(tmp_path):
    """Руль удержан 600 мс и отпущен: метрика считается по удержанию, а не SKIP и не по хвосту."""
    log = _write_csv(tmp_path / "short.csv", _pulse_steer_log(600))
    res = tm.check_yaw_step(log)
    assert len(res) == 1 and not res[0].no_data and res[0].passed is True
    assert "уставка 36.0" in res[0].value
    # Отпускание на 800 мс: хвост после возврата не должен давать settling = 800 мс.
    r = tm.yaw_step_response(_write_csv(tmp_path / "s8.csv", _pulse_steer_log(800)), 49)
    assert r["settling_ms"] < tm.YAW_SETTLING_LIMIT_MS


def test_yaw_step_valueerror_becomes_skip(tmp_path, monkeypatch):
    """Шаг без уставки (ValueError из расчёта) пропускается, а не роняет скрипт."""
    def boom(*a, **k):
        raise ValueError("шаг руля без ненулевой уставки")

    monkeypatch.setattr(tm, "yaw_step_response", boom)
    res = tm.check_yaw_step(_write_csv(tmp_path / "x.csv", _pulse_steer_log(600)))
    assert res[0].no_data and res[0].passed is None


def test_yaw_step_very_short_blip_does_not_crash(tmp_path):
    res = tm.check_yaw_step(_write_csv(tmp_path / "blip.csv", _pulse_steer_log(30)))
    assert len(res) == 1


def test_yaw_step_mode_is_read_after_step(tmp_path):
    """Режим берётся на шаге, а не до него: переключение Normal → Sport одновременно с рулём."""
    rows = _two_steps_log(0.0)
    for i, r in enumerate(rows):
        r["drive_mode"] = 0 if i <= 99 else 1
    assert tm.check_yaw_step(_write_csv(tmp_path / "mode.csv", rows))[0].no_data


def test_gyro_bias_rest_duration_counts_only_contiguous_rest(tmp_path):
    """31 с покоя, 10 мин езды, ещё 31 с: покоя 62 с суммарно, но куски по 31 с."""
    rows = _rest_log(0.52)
    for r in rows:
        r["throttle"] = 0.0
    t_end = rows[-1]["ts_ms"]
    drive = [{"ts_ms": t_end + 10 * (k + 1), "throttle": 0.5, "speed_ms": 2.0,
              "gx": 0.0, "gy": 0.0, "gz": 0.0, "pitch_deg": 0.0, "roll_deg": 0.0}
             for k in range(60000)]
    t2 = drive[-1]["ts_ms"]
    rest2 = [dict(r, ts_ms=t2 + 10 * (k + 1)) for k, r in enumerate(_rest_log(0.52))]
    log = _write_csv(tmp_path / "gap.csv", rows + drive + rest2)
    d = tm.stationary_drift(log)
    assert d["duration_s"] == pytest.approx(2 * 31.2, abs=1.0)  # без 10 мин езды
    d_long = tm.stationary_drift(_write_csv(tmp_path / "one.csv", _rest_log(1.5)))
    assert d_long["duration_s"] == pytest.approx(90, abs=1)


def test_failsafe_short_flag_glitch_is_not_a_loss(tmp_path):
    """Провал rc_ok на 30 мс при езде: grace-период 120 мс не истёк, failsafe не обязан сработать."""
    rows = [{"ts_ms": i * 10, "rc_ok": 0 if 100 <= i < 103 else 1, "wifi_ok": 0,
             "throttle": 0.4, "steering": 0.0} for i in range(300)]
    log = _write_csv(tmp_path / "glitch_ok.csv", rows)
    assert tm.failsafe_latencies(log) == []


@pytest.mark.parametrize("before, after", [(-0.5, 0.3), (-0.4, 0.4), (0.5, -0.3)])
def test_yaw_step_from_held_opposite_turn(tmp_path, before, after):
    """Перекладка руля из удержанного поворота: уставка по новой команде, отклик реально измеряется."""
    gain = tm.NORMAL_STEER_TO_YAW_DPS
    dt = 0.01
    y = before * gain
    rows = []
    for i in range(300):
        cmd = before if i < 50 else after
        y += (cmd * gain - y) * (1 - math.exp(-dt / 0.05))  # τ = 50 мс
        rows.append({"ts_ms": i * 10, "rc_steering": cmd, "yaw_rate_dps": y})
    log = _write_csv(tmp_path / "flip.csv", rows)
    steps = tm.find_steering_steps(log)
    assert len(steps) == 1
    r = tm.yaw_step_response(log, steps[0])
    assert r["setpoint_dps"] == pytest.approx(abs(after) * gain)
    # Реальный отклик: от -45 до +27 °/с за ~3τ, а не мгновенный «PASS 0 мс».
    assert 80 <= r["settling_ms"] <= 200


# ── ESC: газ старта (протокол 4.2) ───────────────────────────────────────

def _staircase_log(start_at: float, n_rest=500, step_ms=2000):
    """Синтетика: газ ступенями 0.02 → 0.20; с `start_at` машина толкается."""
    rows, t = [], 0
    for _ in range(n_rest):
        t += DT_MS
        rows.append({"ts_ms": t, "throttle": 0.0, "ax": 0.002, "ay": -0.001})
    thr = 0.02
    while thr <= 0.2001:
        for k in range(step_ms // DT_MS):
            t += DT_MS
            # Рывок на старте ступени, затухающий за ~0.3 с.
            push = 0.08 * math.exp(-k * DT_MS / 300.0) if thr >= start_at - 1e-9 else 0.0
            rows.append({"ts_ms": t, "throttle": round(thr, 2),
                         "ax": 0.002 + push, "ay": -0.001})
        thr += 0.01
    return rows


def test_esc_start_finds_first_moving_step(tmp_path):
    log = _write_csv(tmp_path / "esc.csv", _staircase_log(start_at=0.07))
    r = tm.esc_start_threshold(log)
    assert r["threshold"] == pytest.approx(0.07)
    assert len(r["steps"]) == 19
    res = tm.check_esc_start(log)
    assert res[0].passed is None and "порог 0.07" in res[0].value


def test_esc_start_never_moving(tmp_path):
    log = _write_csv(tmp_path / "esc.csv", _staircase_log(start_at=1.0))
    assert tm.esc_start_threshold(log)["threshold"] is None
    res = tm.check_esc_start(log)[0]
    assert res.passed is False and "не тронулась" in res.value


def test_esc_start_ignores_imu_orientation_in_plane(tmp_path):
    rows = _staircase_log(start_at=0.05)
    for r in rows:  # IMU повёрнут на 90° вокруг вертикали
        r["ax"], r["ay"] = -r["ay"], r["ax"]
    assert tm.esc_start_threshold(_write_csv(tmp_path / "rot.csv", rows))["threshold"] \
        == pytest.approx(0.05)


def test_esc_start_without_steps(tmp_path):
    rows = [{"ts_ms": (i + 1) * DT_MS, "throttle": 0.0, "ax": 0.0, "ay": 0.0}
            for i in range(500)]
    assert tm.check_esc_start(_write_csv(tmp_path / "x.csv", rows))[0].no_data


# ── Pitch compensation (протокол 7.2) ────────────────────────────────────

def _pitch_log(segments):
    """segments: [(мс, pitch_deg, добавка к газу)] при стике 0.5 с пульта."""
    rows, t = [], 0
    stick = tm.rc_stick_command(0.5)
    for ms, pitch, add in segments:
        for _ in range(ms // DT_MS):
            t += DT_MS
            rows.append({"ts_ms": t, "rc_throttle": 0.5, "cmd_throttle": stick + add,
                         "pitch_deg": pitch, "rc_ok": 1})
    return rows


def test_pitch_comp_classifies_by_nose_up(tmp_path):
    # pitch_deg > 0 — нос вниз: −8° — подъём, +8° — спуск.
    log = _write_csv(tmp_path / "p.csv", _pitch_log(
        [(3000, 0.0, 0.0), (3000, -8.0, 0.12), (3000, 8.0, -0.06)]))
    w = tm.pitch_comp_windows(log)
    assert len(w["uphill"]) == 6 and len(w["downhill"]) == 6 and len(w["flat"]) == 6
    assert [r.passed for r in tm.check_pitch_comp(log)] == [True, True, True]


def test_pitch_comp_wrong_sign_fails_uphill(tmp_path):
    # LOS-369: компенсация с неверным знаком убирает газ в гору.
    log = _write_csv(tmp_path / "p.csv", _pitch_log([(3000, -8.0, -0.12)]))
    res = tm.check_pitch_comp(log)
    assert res[0].passed is False and res[1].no_data and res[2].no_data


def test_pitch_comp_skips_settling_pitch(tmp_path):
    # Тангаж сходится к склону (Madgwick): окна с разбросом > 1° не считаются.
    rows = _pitch_log([(3000, 0.0, 0.0)])
    for k, r in enumerate(rows):
        r["pitch_deg"] = -8.0 * k / len(rows)
    assert not tm.pitch_comp_windows(_write_csv(tmp_path / "p.csv", rows))["uphill"]


def test_rc_stick_command_matches_firmware_deadzone():
    # ApplyRcThrottleDeadzone: 0.08 → 0, 0.5 → 0.4565, −1 → −1.
    assert tm.rc_stick_command(0.08) == 0.0
    assert tm.rc_stick_command(0.5) == pytest.approx(0.42 / 0.92)
    assert tm.rc_stick_command(-1.0) == pytest.approx(-1.0)


def test_pitch_comp_ignores_wifi_and_idle(tmp_path):
    rows = _pitch_log([(3000, -8.0, 0.12)])
    for r in rows[:750]:
        r["rc_ok"] = 0
    for r in rows[750:]:
        r["rc_throttle"] = 0.0
    assert tm.check_pitch_comp(_write_csv(tmp_path / "p.csv", rows))[0].no_data


@needs_sim
def test_pitch_comp_on_sil_climb(tmp_path):
    """SIL: тот же подъём, что в test_pitch_comp_adds_throttle_uphill."""
    with ClosedLoopSim(BIN, dynamic=True, sensor_noise=False, stabilize=True,
                       pitch_comp=True) as sim:
        sim.run(1000, rc_throttle=0.0, rc_steering=0.0)
        flat = sim.run(2000, rc_throttle=0.5, rc_steering=0.0)
        sim.set_slope(10.0)
        climb = sim.run(3000, rc_throttle=0.5, rc_steering=0.0)
    log = _write_csv(tmp_path / "sil.csv", _sil_to_log(flat + climb, rc_ok=1))
    w = tm.pitch_comp_windows(log)
    assert w["uphill"], "SIL-подъём не распознан по pitch_deg"
    assert sum(w["uphill"]) / len(w["uphill"]) > tm.PITCH_UPHILL_MIN_ADD


# ── Oversteer guard (протокол 7.3) ───────────────────────────────────────

@needs_sim
def test_oversteer_on_sil_corner(tmp_path):
    """SIL: тот же поворот, что в test_oversteer_guard_cuts_throttle_and_slip."""
    from simlib.closed_loop import OversteerReplayConfig

    def corner(reduction):
        with ClosedLoopSim(BIN, dynamic=True, stabilize=True, sensor_noise=False,
                           oversteer=OversteerReplayConfig(5.0, 10.0, reduction)) as sim:
            sim.run(2000, rc_throttle=0.5, rc_steering=0.0)
            rows = sim.run(1500, rc_throttle=1.0, rc_steering=1.0)
        return _write_csv(tmp_path / f"os{reduction}.csv", _sil_to_log(
            rows, oversteer_active=lambda i, r: r["oversteer_active"],
            slip_deg=lambda i, r: r["ekf_slip_deg"]))

    guarded = tm.check_oversteer(corner(0.7))
    assert guarded and all(r.passed for r in guarded)
    # reduction = 0: детектор срабатывает, газ не срезан — так и должно выйти.
    detect_only = tm.check_oversteer(corner(0.0))
    assert detect_only and not any(r.passed for r in detect_only)


def test_oversteer_merges_short_gaps(tmp_path):
    rows = []
    for i in range(600):
        act = 1 if 100 <= i < 200 or 250 <= i < 300 or 500 <= i < 550 else 0
        rows.append({"ts_ms": (i + 1) * DT_MS, "oversteer_active": act,
                     "rc_throttle": 1.0, "cmd_throttle": 0.6 if act else 1.0,  # стик 1 → 1
                     "slip_deg": 12.0 if act else 2.0})
    eps = tm.oversteer_episodes(_write_csv(tmp_path / "o.csv", rows))
    assert len(eps) == 2  # разрыв 100 мс склеен, 400 мс — нет
    assert eps[0]["max_cut"] == pytest.approx(0.4)
    assert eps[0]["max_slip_deg"] == pytest.approx(12.0)


def test_oversteer_without_episodes(tmp_path):
    rows = [{"ts_ms": (i + 1) * DT_MS, "oversteer_active": 0, "rc_throttle": 1.0,
             "cmd_throttle": 1.0, "slip_deg": 1.0} for i in range(100)]
    assert tm.check_oversteer(_write_csv(tmp_path / "o.csv", rows))[0].no_data


# ── События RC (протокол 2.5) ────────────────────────────────────────────

def _event_log(events):
    """events: {индекс: (тип, param, v1, v2)} поверх 1000 кадров."""
    rows = []
    for i in range(1000):
        ev = events.get(i, ("", "", "", ""))
        rows.append({"ts_ms": (i + 1) * DT_MS, "throttle": 0.0,
                     "event_type": ev[0], "event_param": ev[1],
                     "event_value1": ev[2], "event_value2": ev[3]})
    return rows


def test_rc_events_pairs_loss_and_restore(tmp_path):
    log = _write_csv(tmp_path / "e.csv", _event_log({
        10: ("RcRestored", "first_acquire", 0, 0),
        100: ("RcLost", "thr=no_pulses str=no_pulses", 1500, 1500),
        300: ("RcRestored", "", 0.4, 2),
        500: ("RcLost", "thr=out_of_range str=ok", 2300, 1500),
        600: ("RcRestored", "", 0.2, 0)}))
    res = tm.check_rc_events(log)
    assert [r.passed for r in res] == [True, True, None]
    assert "no_pulses" in res[0].value and "0.40 с" in res[0].value
    assert res[-1].value.startswith("2 ")


def test_rc_events_unrestored_and_double_loss(tmp_path):
    log = _write_csv(tmp_path / "e.csv", _event_log({
        100: ("RcLost", "thr=no_pulses str=no_pulses", 0, 0),
        300: ("RcLost", "thr=no_pulses str=no_pulses", 0, 0)}))
    res = tm.check_rc_events(log)
    assert res[0].passed is False  # вторая потеря без восстановления
    assert res[1].passed is False and "не восстановлена" in res[1].value


def test_rc_events_split_coalesced_frame(tmp_path):
    # Два события в одном кадре web UI склеивает через «|».
    log = _write_csv(tmp_path / "e.csv", _event_log({
        100: ("TestStart|RcLost", "|thr=no_pulses str=ok", "0|1500", "0|1500"),
        300: ("RcRestored", "", 0.4, 0)}))
    res = tm.check_rc_events(log)
    assert [r.passed for r in res] == [True, None]
    assert "1500/1500" in res[0].value


def test_rc_events_incomplete_diagnostics_fail(tmp_path):
    log = _write_csv(tmp_path / "e.csv", _event_log({
        100: ("RcLost", "", 1500, 1500),
        300: ("RcRestored", "", 0.4, 0),
        500: ("RcLost", "thr=no_pulses str=ok", "", 1500),
        600: ("RcRestored", "", 0.2, 0)}))
    res = tm.check_rc_events(log)
    assert [r.passed for r in res] == [False, False, None]


def test_rc_events_ignore_startup_without_transmitter(tmp_path):
    log = _write_csv(tmp_path / "e.csv", _event_log({
        10: ("RcLost", "thr=no_pulses str=no_pulses since_boot", 0, 0),
        50: ("RcRestored", "first_acquire", 0, 0),
        100: ("RcLost", "thr=no_pulses str=no_pulses", 1500, 1500),
        300: ("RcRestored", "", 0.4, 0)}))
    res = tm.check_rc_events(log)
    assert [r.passed for r in res] == [True, None]
    assert res[-1].value.startswith("1 ")


def test_rc_events_without_event_columns(tmp_path):
    rows = [{"ts_ms": (i + 1) * DT_MS, "throttle": 0.0} for i in range(10)]
    assert tm.check_rc_events(_write_csv(tmp_path / "e.csv", rows))[0].no_data
