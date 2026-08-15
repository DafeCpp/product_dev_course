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
        "speed_meas": rows[-1]["ekf_speed_meas"],
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


def test_kids_speed_limiter_reduces_output_without_changing_motor_anchor():
    """LOS-246: limiter режет PWM, но не измерение моторной модели EKF."""
    unlimited = _full_throttle(drive_mode="kids")
    limited = _full_throttle(drive_mode="kids", speed_limit=1.0)

    # LOS-285: потолок — throttle, на котором сама мотор-модель предсказывает
    # ровно speed_limit (deadzone/motor_speed_gain — дефолты прошивки,
    # common/stabilization_config.hpp: FilterConfig::motor_deadzone=0.05,
    # motor_speed_gain=8.0). До фикса limiter сходился к фиксированным 50%
    # команды независимо от speed_limit — теперь целится в настроенный
    # предел напрямую, поэтому и потолок предсказуем по формуле, а не по
    # произвольной доле unlimited["thr"].
    deadzone, motor_speed_gain, speed_limit = 0.05, 8.0, 1.0
    expected_cap = deadzone + speed_limit * (1 - deadzone) / motor_speed_gain
    assert limited["thr"] == pytest.approx(expected_cap, abs=0.01)

    # Моторный якорь получает исходную команду до лимитеров. Если подать сюда
    # урезанный PWM, как до LOS-246, speed_meas и EKF начнут следовать за
    # выходом лимитера, снова замыкая положительную обратную связь.
    assert limited["speed_meas"] == pytest.approx(
        unlimited["speed_meas"], rel=0.01)
    assert limited["final_ekf"] == pytest.approx(limited["speed_meas"], rel=0.01)
