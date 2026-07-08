"""Closed-loop SIL регресс FW-R22: рулевая стабилизация в реверсе.

Баг: в реверсе связь руль→рыскание инвертируется, yaw-rate обратная связь
становится положительной → стабилизатор гонит руль в насыщение (вместо
отслеживания команды). Фикс отключает yaw-стабилизацию при заднем ходе.

Тест включает стабилизацию (`--stabilize`, иначе stab_weight=0 и контроллеры
не работают) и проверяет, что в реверсе applied steering отслеживает команду,
а не уходит в насыщение.
"""

import pytest

from simlib import ClosedLoopSim, find_sim_host

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")


def _settled_steering(thr: float, steer: float, **kw) -> float:
    with ClosedLoopSim(BIN, **kw) as sim:
        rows = sim.run(1500, dt_ms=2, rc_throttle=thr, rc_steering=steer)
    return rows[-1]["steering"]


def test_stabilization_engages_with_flag():
    """Sanity: --stabilize реально включает yaw-стабилизацию (вперёд)."""
    free = _settled_steering(0.5, 0.2)                  # выкл → pass-through
    stab = _settled_steering(0.5, 0.2, stabilize=True)  # вкл → корректирует
    assert abs(free - 0.2) < 1e-3   # без стабилизации руль проходит как есть
    assert stab < free * 0.9        # с стабилизацией yaw-стаб заметно корректирует (≥10%)


def test_reverse_no_steering_saturation():
    """FW-R22: в реверсе руль отслеживает команду, не уходит в насыщение."""
    pos = _settled_steering(-0.5, 0.2, stabilize=True)
    neg = _settled_steering(-0.5, -0.2, stabilize=True)
    # Фикс: ~±0.2 (стаб off в реверсе → pass-through). Баг: ~±0.5 (насыщение).
    assert abs(pos - 0.2) < 0.1
    assert abs(neg + 0.2) < 0.1


def test_reverse_straight_no_phantom_steering():
    """Реверс без руля → applied steering остаётся ~0 (нет автоколебаний)."""
    applied = _settled_steering(-0.5, 0.0, stabilize=True)
    assert abs(applied) < 1e-3
