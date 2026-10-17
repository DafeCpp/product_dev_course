"""LOS-362: closed-loop сценарии с критериями из TESTING_PROGRAM.md.

Те же критерии PASS, что проверяются на машине, но в SIL: это регрессия и
эталон для сравнения с будущими заездами. Не покрыто (нет поддержки в модели
или sim_host): склон/pitch compensation, удержание угла заноса в Drift,
anti-spin, пресеты Kids; см. LOS-362.
"""

import pytest

from simlib import ClosedLoopSim, OversteerReplayConfig, find_sim_host

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")

DT_MS = 2


def _first_ms(rows, pred):
    for i, row in enumerate(rows):
        if pred(row):
            return (i + 1) * DT_MS
    return None


def test_rc_loss_neutral_within_250_ms_and_recovers():
    """Failsafe: потеря RC и Wi-Fi → нейтраль за ≤250 мс, возврат команды снимает failsafe."""
    with ClosedLoopSim(BIN, sensor_noise=False) as sim:
        sim.run(500, rc_throttle=0.5, rc_steering=0.3)
        lost = sim.run(300, rc_throttle=None, rc_steering=None)
        neutral_ms = _first_ms(
            lost, lambda r: r["failsafe"] == 1.0 and r["throttle"] == 0.0
            and r["steering"] == 0.0)
        assert neutral_ms is not None and neutral_ms <= 250
        assert all(r["throttle"] == 0.0 and r["steering"] == 0.0
                   for r in lost[neutral_ms // DT_MS:])

        back = sim.run(300, rc_throttle=0.5, rc_steering=0.3)
    assert _first_ms(back, lambda r: r["failsafe"] == 0.0) <= 20
    assert back[-1]["throttle"] > 0.3


def test_yaw_rate_step_response_normal_mode():
    """Yaw PID (Normal): резкий поворот руля на ходу, overshoot < 20 %, settling < 200 мс."""
    with ClosedLoopSim(BIN, stabilize=True, sensor_noise=False) as sim:
        sim.run(1500, rc_throttle=0.4, rc_steering=0.0)
        rows = sim.run(500, rc_throttle=0.4, rc_steering=0.8)
    yaw = [r["ekf_yaw_rate"] for r in rows]
    final = sum(yaw[-50:]) / 50
    assert final > 0.5  # машина действительно поворачивает

    overshoot = (max(yaw) - final) / final
    assert overshoot < 0.20

    out_of_band = [i for i, y in enumerate(yaw) if abs(y - final) > 0.1 * final]
    settling_ms = (out_of_band[-1] + 1) * DT_MS
    assert settling_ms < 200


def test_oversteer_guard_cuts_throttle_and_slip():
    """Oversteer guard: резкий газ в повороте → guard срабатывает, газ и занос снижаются."""
    guard = OversteerReplayConfig(5.0, 10.0, 0.7)

    def corner(**kw):
        with ClosedLoopSim(BIN, dynamic=True, stabilize=True,
                           sensor_noise=False, **kw) as sim:
            sim.run(2000, rc_throttle=0.5, rc_steering=0.0)
            return sim.run(1500, rc_throttle=1.0, rc_steering=1.0)

    free = corner()
    guarded = corner(oversteer=guard)
    assert all(r["oversteer_active"] == 0.0 for r in free)
    assert any(r["oversteer_active"] == 1.0 for r in guarded)
    assert (max(abs(r["ekf_slip_deg"]) for r in guarded)
            < max(abs(r["ekf_slip_deg"]) for r in free))
    # Тот же шаг без guard даёт газ выше: сравниваем по одному и тому же тику.
    assert any(g["oversteer_active"] == 1.0 and g["throttle"] < f["throttle"] - 0.1
               for f, g in zip(free, guarded))


def test_kids_accel_limiter_smooths_launch():
    """Kids accel limiter: газ в пол с нуля → лимитер включается, газ режется до throttle_limit."""
    with ClosedLoopSim(BIN, drive_mode="kids", speed_limit=3.0,
                       sensor_noise=False) as sim:
        rows = sim.run(2000, rc_throttle=1.0, rc_steering=0.0)
    assert any(r["kids_accel_limit_active"] == 1.0 for r in rows)
    assert max(abs(r["throttle"]) for r in rows) <= 0.31
    assert max(r["forward_accel"] for r in rows) < 0.3 * 9.81
