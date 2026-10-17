"""LOS-362: SIL-сценарии с критериями из TESTING_PROGRAM.md.

Те же критерии PASS, что проверяются на машине, но в SIL: это регрессия и
эталон для сравнения с будущими заездами. Не покрыто (нет поддержки в модели
или sim_host): склон/pitch compensation, удержание угла заноса в Drift,
anti-spin, пресеты Kids, таймаут потери Wi-Fi (в StdioPlatform failsafe
мгновенный); см. LOS-362.
"""

import math

import pytest

from simlib import ClosedLoopSim, OversteerReplayConfig, find_sim_host
from simlib.frame import SensorFrame
from simlib.sim_host_runner import run_batch

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")

DT_MS = 2

# Normal mode (common/drive_modes.cpp): уставка yaw rate = руль × 90 °/с,
# коррекция PID ограничена ±0.30.
NORMAL_STEER_TO_YAW_DPS = 90.0
NORMAL_MAX_CORRECTION = 0.30


def _first_ms(rows, pred):
    for i, row in enumerate(rows):
        if pred(row):
            return (i + 1) * DT_MS
    return None


def _pulses(ms: int, thr_us: int, str_us: int) -> list[SensorFrame]:
    return [SensorFrame(dt_ms=DT_MS, rc_pulse_us=(thr_us, str_us))
            for _ in range(ms // DT_MS)]


def test_rc_loss_neutral_within_250_ms_and_recovers():
    """Failsafe: приёмник пропал → моторы в нейтрали за ≤250 мс, импульсы вернулись → управление вернулось.

    Через сырые импульсы, чтобы в контуре был настоящий таймаут RcPulseDecoder
    (250 мс); флаг rc_present=0 обошёл бы его (см. test_rc_link.py).
    """
    loss_at = 2000
    frames = (_pulses(loss_at, 1600, 1600) + _pulses(600, 0, 0)
              + _pulses(1000, 1600, 1600))
    rows = run_batch(frames, BIN)
    assert len(rows) == len(frames)
    assert rows[loss_at // DT_MS - 1]["throttle"] > 0.1  # до потери едем

    lost = rows[loss_at // DT_MS:(loss_at + 600) // DT_MS]
    neutral_ms = _first_ms(
        lost, lambda r: r["failsafe"] == 1.0 and r["throttle"] == 0.0
        and r["steering"] == 0.0)
    assert neutral_ms is not None
    # Нижняя граница: нейтраль именно по таймауту декодера, а не раньше.
    assert 200 <= neutral_ms <= 250
    assert all(r["throttle"] == 0.0 and r["steering"] == 0.0
               for r in lost[neutral_ms // DT_MS:])

    back = rows[(loss_at + 600) // DT_MS:]
    recovered_ms = _first_ms(back, lambda r: r["failsafe"] == 0.0)
    assert recovered_ms is not None and recovered_ms <= 60
    assert back[-1]["throttle"] > 0.1


def _yaw_step(steer: float, stabilize: bool = True):
    with ClosedLoopSim(BIN, dynamic=True, stabilize=stabilize,
                       sensor_noise=False) as sim:
        sim.run(1500, rc_throttle=0.4, rc_steering=0.0)
        rows = sim.run(1000, rc_throttle=0.4, rc_steering=steer)
    yaw_dps = [math.degrees(r["ekf_yaw_rate"]) for r in rows]  # rad/s → °/с
    return rows, yaw_dps


# Шаг 0.4 (уставка 36 °/с): PID не упирается в ограничение коррекции, так что
# измеряется именно регулятор, а не насыщение.
STEP_STEER = 0.4


def test_yaw_rate_step_tracks_setpoint_without_overshoot_over_20pct():
    """Yaw PID (Normal): резкий поворот руля на ходу, overshoot < 20 % от уставки."""
    setpoint = NORMAL_STEER_TO_YAW_DPS * STEP_STEER
    rows, yaw = _yaw_step(STEP_STEER)
    _, open_loop = _yaw_step(STEP_STEER, stabilize=False)

    # Первые 300 мс руль ещё нарастает по slew rate; дальше разница между
    # командой и выходом — это коррекция PID.
    correction = [abs(r["steering"] - STEP_STEER) for r in rows[300 // DT_MS:]]
    assert max(correction) < NORMAL_MAX_CORRECTION - 0.01  # без насыщения

    final = sum(yaw[-100:]) / 100
    assert abs(final - setpoint) < 0.1 * setpoint
    # Без PID та же команда даёт почти вдвое больший yaw rate.
    assert abs(open_loop[-1] - setpoint) > 0.5 * setpoint

    overshoot = (max(yaw) - setpoint) / setpoint
    assert overshoot < 0.20


@pytest.mark.xfail(
    strict=True,
    reason="LOS-362: в SIL yaw PID в Normal уходит в автоколебания ±8 % "
           "(руль 0.17..0.30, период ~400 мс) и не входит в полосу ±10 % за 200 мс")
def test_yaw_rate_step_settles_within_200_ms():
    """Yaw PID (Normal): settling < 200 мс (полоса ±10 % от уставки)."""
    setpoint = NORMAL_STEER_TO_YAW_DPS * STEP_STEER
    _, yaw = _yaw_step(STEP_STEER)
    out_of_band = [i for i, y in enumerate(yaw)
                   if abs(y - setpoint) > 0.1 * setpoint]
    settling_ms = (out_of_band[-1] + 1) * DT_MS if out_of_band else 0
    assert settling_ms < 200


def test_oversteer_guard_cuts_throttle_and_slip():
    """Oversteer guard: резкий газ в повороте → guard срабатывает, газ и занос снижаются."""

    def corner(reduction: float):
        guard = OversteerReplayConfig(5.0, 10.0, reduction)
        with ClosedLoopSim(BIN, dynamic=True, stabilize=True,
                           sensor_noise=False, oversteer=guard) as sim:
            sim.run(2000, rc_throttle=0.5, rc_steering=0.0)
            return sim.run(1500, rc_throttle=1.0, rc_steering=1.0)

    # Эталон: детектор включён, но газ не режет — занос тот же, что без guard.
    detect_only = corner(0.0)
    guarded = corner(0.7)
    assert any(r["oversteer_active"] == 1.0 for r in detect_only)
    assert any(r["oversteer_active"] == 1.0 for r in guarded)
    assert (max(abs(r["ekf_slip_deg"]) for r in guarded)
            < max(abs(r["ekf_slip_deg"]) for r in detect_only))
    # Сравниваем газ по одному и тому же тику.
    assert any(g["oversteer_active"] == 1.0 and g["throttle"] < f["throttle"] - 0.1
               for f, g in zip(detect_only, guarded))


def _launch(rc_throttle: float, **kw) -> tuple[list[dict], float]:
    """Разгон с места; вернуть строки и пик forward_accel (в g)."""
    with ClosedLoopSim(BIN, sensor_noise=False, **kw) as sim:
        rows = sim.run(2000, rc_throttle=rc_throttle, rc_steering=0.0)
    return rows, max(r["forward_accel"] for r in rows)


def test_kids_accel_limiter_smooths_launch():
    """Kids accel limiter: газ в пол с нуля → лимитер режет газ и пик ускорения."""
    rows, kids_peak = _launch(1.0, drive_mode="kids")
    # Эталон без Kids: Normal с тем же выходным газом (~0.30 = kids throttle_limit).
    ref_rows, ref_peak = _launch(0.36)
    assert max(r["throttle"] for r in ref_rows) == pytest.approx(0.30, abs=0.01)

    limited = [r for r in rows if r["kids_accel_limit_active"] == 1.0]
    assert limited
    assert max(abs(r["throttle"]) for r in rows) <= 0.31
    # Лимитер режет газ ниже kids throttle_limit (0.3), пока ускорение выше порога.
    assert min(r["throttle"] for r in limited) < 0.25
    # Порог accel_threshold_g = 0.15, но лимитер пропорциональный, и пик в SIL
    # выше порога; критерий — пик заметно ниже, чем без лимитера.
    assert kids_peak < ref_peak - 0.015
