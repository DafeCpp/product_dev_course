import math

import pytest

from simlib import ClosedLoopSim, find_sim_host

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")


def test_throttle_ramp_increases_speed():
    with ClosedLoopSim(BIN) as sim:
        rows = sim.run(600, rc_throttle=0.5, rc_steering=0.0)
    assert sim.model.state.v > 0.5
    assert rows[-1]["failsafe"] == 0.0


def test_step_steer_turns_symmetric():
    with ClosedLoopSim(BIN) as left:
        left.run(600, rc_throttle=0.4, rc_steering=0.5)
        psi_left = left.model.state.psi
    with ClosedLoopSim(BIN) as right:
        right.run(600, rc_throttle=0.4, rc_steering=-0.5)
        psi_right = right.model.state.psi
    assert psi_left > 0.05  # руль вправо по конвенции δ>0 → ψ растёт
    assert psi_right < -0.05
    assert math.isclose(psi_left, -psi_right, rel_tol=1e-3)


def test_failsafe_neutral_when_no_command():
    with ClosedLoopSim(BIN) as sim:
        rows = sim.run(300, rc_throttle=None, rc_steering=None)
    assert rows[-1]["failsafe"] == 1.0
    assert abs(rows[-1]["throttle"]) < 1e-6
    assert abs(rows[-1]["steering"]) < 1e-6
    assert abs(sim.model.state.v) < 1e-3  # без тяги машина стоит


def test_no_phantom_steering_after_boot():
    """FW-R17 регресс: прямая команда с буста → нет фантомного руля."""
    with ClosedLoopSim(BIN) as sim:
        rows = sim.run(400, rc_throttle=0.1, rc_steering=0.0)
    assert max(abs(r["steering"]) for r in rows) < 1e-4


def test_reverse_flips_turn_direction():
    """FW-R22-зона: тот же руль на заднем ходу → противоположный поворот."""
    with ClosedLoopSim(BIN) as fwd:
        fwd.run(600, rc_throttle=0.4, rc_steering=0.5)
    with ClosedLoopSim(BIN) as rev:
        rev.run(600, rc_throttle=-0.4, rc_steering=0.5)
    assert fwd.model.state.psi > 0.0
    assert rev.model.state.psi < 0.0


def test_deterministic_runs():
    def go():
        with ClosedLoopSim(BIN) as s:
            rows = s.run(200, rc_throttle=0.4, rc_steering=0.3)
        return [(round(r["throttle"], 6), round(r["steering"], 6)) for r in rows]

    assert go() == go()


def test_outputs_finite_and_in_range():
    with ClosedLoopSim(BIN) as sim:
        rows = sim.run(
            500,
            rc_throttle=lambda i: 0.3 + 0.1 * math.sin(i * 0.05),
            rc_steering=lambda i: 0.5 * math.sin(i * 0.03),
        )
    for r in rows:
        assert math.isfinite(r["throttle"]) and -1.0001 <= r["throttle"] <= 1.0001
        assert math.isfinite(r["steering"]) and -1.0001 <= r["steering"] <= 1.0001
        assert math.isfinite(r["yaw_deg"])
        assert math.isfinite(r["ekf_vx"])
