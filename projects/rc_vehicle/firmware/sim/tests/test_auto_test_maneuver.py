"""Closed-loop тест авто-манёвров start_test (регресс LOS-214).

Машина при `start_test` не разгонялась, а `target_accel` ни на что не влиял.
Причина — постоянный офсет в продольном ускорении (завал калиброванной оси
«вперёд» либо уклон под машиной): он превышал порог breakaway, отрыв
«детектировался» на 25-м тике на стоящей машине, газ замерзал на ~0.02–0.03,
а уставка PI оказывалась смещённой на величину офсета.

Здесь офсет создаётся физически — наклоном IMU (`imu_pitch_deg`), поэтому
тест гоняет ровно тот тракт, что и на железе: сырой accel → калибровка →
VehicleStateEstimator (компенсация гравитации по тангажу) → MotionDriver.

На старом коде при pitch=-8° и target=0.1g газ застревал на 0.021, а машина
не трогалась вообще (model_v = 0.0000).
"""

from dataclasses import replace

import pytest

from simlib import ClosedLoopSim, find_sim_host, get_profile
from simlib.closed_loop import DEFAULT_CLOSED_LOOP_PROFILE
from simlib.sim_params import SimParams

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")

# Наклон IMU носом вниз → положительный офсет продольного ускорения в покое
# (выше порога breakaway 0.03g). Знак важен: при обратном наклоне офсет
# наоборот завышает газ и баг не проявляется.
TILTED = replace(
    get_profile(DEFAULT_CLOSED_LOOP_PROFILE), imu_pitch_deg=-8.0)


def _run_straight(target_accel: float, params: SimParams | None = None,
                  ticks: int = 3000) -> dict:
    """Прогнать Straight-манёвр; вернуть сводку прогона."""
    with ClosedLoopSim(BIN, params=params,
                       start_test="straight", target_accel=target_accel,
                       test_duration=3.0, sensor_noise=False) as sim:
        rows = sim.run(ticks, dt_ms=2)
    return {
        "peak_throttle": max(r["throttle"] for r in rows),
        "model_v": sim.model.state.v,
        "peak_ekf": max(r["ekf_speed_ms"] for r in rows),
        "rest_forward_accel": rows[0]["forward_accel"],
        "test_ticks": sum(int(r["test_active"]) for r in rows),
        "failsafe": rows[-1]["failsafe"],
    }


def test_auto_test_runs_without_rc():
    """Манёвр получает тики и ведёт машину без единой RC-команды."""
    r = _run_straight(0.2)
    assert r["failsafe"] == 0.0, "failsafe погасил PWM — нет keepalive"
    assert r["test_ticks"] > 1000, "тест не был активен"
    # На measured drag=1.128 установившаяся скорость ниже старого baseline,
    # но при исправном авто-манёвре машина уверенно отрывается от нуля.
    assert r["model_v"] > 0.25, "машина не тронулась"


def test_tilted_imu_still_accelerates():
    """Статический офсет ускорения не должен блокировать разгон.

    Это основной регресс: раньше офсет давал ложный отрыв, газ застревал
    на ~0.02 и машина стояла.
    """
    r = _run_straight(0.1, params=TILTED)

    assert r["rest_forward_accel"] > 0.03, (
        "сценарий не воспроизводит условие бага: офсет в покое ниже порога "
        "breakaway, наклон IMU задан неверно")
    assert r["peak_throttle"] > 0.1, (
        f"газ застрял на {r['peak_throttle']:.4f} — ложный отрыв (LOS-214)")
    assert r["model_v"] > 0.25, "машина не разогналась при наклонённом IMU"


def test_target_accel_controls_throttle_when_tilted():
    """Больше target_accel → больше газ и скорость, даже с офсетом."""
    low = _run_straight(0.1, params=TILTED)
    high = _run_straight(0.3, params=TILTED)

    assert high["peak_throttle"] > low["peak_throttle"], (
        "target_accel не влияет на газ (симптом LOS-214)")
    assert high["model_v"] > low["model_v"]


def test_tilt_does_not_change_outcome():
    """Наклон IMU не должен менять результат манёвра.

    baseline вычитается, поэтому прогон с наклоном и без совпадает.
    """
    flat = _run_straight(0.2)
    tilted = _run_straight(0.2, params=TILTED)

    # Оценка наклона/ускорения не идеальна, но компенсация должна удерживать
    # результат в том же рабочем режиме. До LOS-214 наклон давал speed=0.
    assert tilted["peak_throttle"] == pytest.approx(
        flat["peak_throttle"], rel=0.15)
    assert tilted["model_v"] == pytest.approx(flat["model_v"], rel=0.25)
