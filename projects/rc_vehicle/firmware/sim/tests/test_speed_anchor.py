"""Closed-loop тесты якоря скорости EKF (мотор-модель + NHC + guard, LOS-233).

Проверяем, что оценка скорости EKF трекает истинную скорость физмодели и не
уходит в разнос, а флаг расходимости не срабатывает в нормальной езде.
"""

import math

import pytest

from simlib import ClosedLoopSim, find_sim_host

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")

# Физический максимум скорости из VehicleEkf::kMaxSpeedMs (guard).
K_MAX_SPEED_MS = 15.0


def test_ekf_speed_tracks_true_speed():
    """На установившемся газе EKF-скорость близка к истинной скорости модели."""
    with ClosedLoopSim(BIN) as sim:
        rows = sim.run(2500, dt_ms=2, rc_throttle=0.5, rc_steering=0.0)
    true_v = abs(sim.model.state.v)
    ekf_v = rows[-1]["ekf_speed_ms"]
    assert true_v > 1.0  # модель реально разогналась
    # Трекинг в пределах допуска (слабый якорь + шум мотор-модели).
    assert abs(ekf_v - true_v) < max(1.5, 0.35 * true_v), (ekf_v, true_v)


def test_ekf_speed_bounded_no_divergence():
    """Переменный газ: скорость в физпределах, guard не срабатывает."""
    with ClosedLoopSim(BIN) as sim:
        rows = sim.run(
            3000,
            dt_ms=2,
            rc_throttle=lambda i: 0.4 + 0.3 * math.sin(i * 0.01),
            rc_steering=lambda i: 0.3 * math.sin(i * 0.02),
        )
    assert max(r["ekf_speed_ms"] for r in rows) < K_MAX_SPEED_MS
    # ekf_vx_var не залипает на потолке (в проблемном логе доходил до 1000).
    assert max(r["ekf_vx_var"] for r in rows) < 100.0
    # Guard не должен срабатывать в нормальной езде.
    assert all(r["ekf_diverged"] == 0.0 for r in rows)


def test_speed_meas_reflects_throttle():
    """ekf_speed_meas (мотор-модель) растёт с газом и обнуляется в мёртвой зоне."""
    with ClosedLoopSim(BIN) as sim:
        rows = sim.run(1500, dt_ms=2, rc_throttle=0.6, rc_steering=0.0)
    assert rows[-1]["ekf_speed_meas"] > 1.0
    with ClosedLoopSim(BIN) as idle:
        rows_idle = idle.run(300, dt_ms=2, rc_throttle=0.0, rc_steering=0.0)
    assert abs(rows_idle[-1]["ekf_speed_meas"]) < 1e-6
