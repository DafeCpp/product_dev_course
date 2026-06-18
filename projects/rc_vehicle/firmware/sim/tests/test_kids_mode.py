"""Closed-loop тест детского режима: лимит скорости (регресс FW-R21).

Полный газ в Kids-режиме → ограничения детского режима срабатывают (газ режется
до throttle_limit, лимитер скорости держит скорость у заданного порога), в отличие
от Normal. Это регресс-кейс FW-R21 (ранее лимит детского режима не применялся из-за
висячего указателя на конфиг в KidsModeProcessor).
"""

import pytest

from simlib import ClosedLoopSim, find_sim_host

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")


def _full_throttle(**kw) -> dict:
    """Прогнать полный газ; вернуть сводку (последняя строка + пик/модель)."""
    with ClosedLoopSim(BIN, **kw) as sim:
        rows = sim.run(2500, dt_ms=2, rc_throttle=1.0, rc_steering=0.0)
    return {
        "model_v": sim.model.state.v,
        "peak_ekf": max(r["ekf_speed_ms"] for r in rows),
        "final_ekf": rows[-1]["ekf_speed_ms"],
        "thr": rows[-1]["throttle"],
        "kids": rows[-1]["kids_mode_active"],
    }


def test_kids_mode_limits_speed_vs_normal():
    normal = _full_throttle()
    kids = _full_throttle(drive_mode="kids", speed_limit=1.0)
    assert normal["kids"] == 0.0
    assert kids["kids"] == 1.0
    assert normal["model_v"] > 7.0  # Normal разгоняется свободно
    assert kids["model_v"] < 0.5 * normal["model_v"]  # Kids жёстко ограничен
    assert kids["thr"] <= 0.31  # газ срезан до kids throttle_limit (0.3)


def test_kids_speed_limit_value_controls_speed():
    """Чем ниже лимит скорости — тем ниже установившаяся скорость (лимитер EKF)."""
    low = _full_throttle(drive_mode="kids", speed_limit=1.0)
    high = _full_throttle(drive_mode="kids", speed_limit=2.5)
    assert low["final_ekf"] < high["final_ekf"]
    assert low["peak_ekf"] < 2.0  # при лимите 1.0 скорость EKF держится у порога
