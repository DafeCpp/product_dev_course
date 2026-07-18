"""Plausibility-тесты работоспособности модели в контуре (closed-loop SIL).

Простые «здравые» сценарии: газ N секунд → отпустил → накат до остановки и т.п.
"""

import pytest

from simlib import ClosedLoopSim, find_sim_host

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")


def test_drive_then_release_decelerates_to_stop():
    """Газ 2 с → отпустил → машина разгоняется, затем накатом тормозит до ~0."""
    with ClosedLoopSim(BIN) as sim:
        accel = sim.run(1000, dt_ms=2, rc_throttle=0.5, rc_steering=0.0)  # 2 с
        peak_v = sim.model.state.v
        # Отпустить газ (команда есть, но 0 → не failsafe): накат на сопротивлении.
        sim.run(4000, dt_ms=2, rc_throttle=0.0, rc_steering=0.0)  # 8 с
        final_v = sim.model.state.v
    assert peak_v > 2.0  # разогналась
    assert accel[-1]["throttle"] > 0.3  # под газом throttle подан
    assert final_v < 0.1  # накатом остановилась
    assert final_v < 0.05 * peak_v


def test_coast_without_command_stays_stopped():
    """Нет команды (failsafe) → нейтраль, машина стоит."""
    with ClosedLoopSim(BIN) as sim:
        rows = sim.run(500, dt_ms=2, rc_throttle=None, rc_steering=None)
    assert rows[-1]["failsafe"] == 1.0
    assert abs(sim.model.state.v) < 1e-3


def test_brake_reverses_with_negative_throttle():
    """Разгон вперёд → отрицательный газ → тормозит и едет назад."""
    with ClosedLoopSim(BIN) as sim:
        sim.run(800, dt_ms=2, rc_throttle=0.5, rc_steering=0.0)
        assert sim.model.state.v > 1.0
        sim.run(1500, dt_ms=2, rc_throttle=-0.5, rc_steering=0.0)
        assert sim.model.state.v < 0.0  # поехала назад
