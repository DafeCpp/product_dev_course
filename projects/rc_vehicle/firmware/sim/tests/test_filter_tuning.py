"""LOS-384: Madgwick β и частота среза LPF gz в SIL (пункты 7.7 и 7.8).

На машине β и cutoff подбирают по компромиссу, и здесь проверяется, что
флаги --madgwick-beta и --lpf-cutoff-hz доходят до прошивки и дают его в
нужную сторону.

β (7.7, adaptive β выключен, как в протоколе): малый β медленнее догоняет
уклон, большой сильнее уводит pitch/roll ускорениями разгона и поворота.

LPF (7.8): низкий cutoff сильнее давит вибрацию в gz, но запаздывает на шаге
yaw rate. Прогон batch на синтетическом gz, без физмодели: в модели gz от
тряски дороги нулевой.
"""

import math

import pytest

from simlib import ClosedLoopSim, find_sim_host
from simlib.frame import SensorFrame
from simlib.sim_host_runner import run_batch

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")

DT_MS = 2
SETTLE_TICKS = 4500
SLOPE_DEG = 10.0


def _slope_settle_ms(beta: float) -> int:
    """Время, за которое pitch доходит до 63 % уклона, встав на склон."""
    with ClosedLoopSim(BIN, madgwick_beta=beta, sensor_noise=False) as sim:
        sim.run(SETTLE_TICKS, DT_MS, rc_throttle=0.0, rc_steering=0.0)
        sim.set_slope(SLOPE_DEG)
        rows = sim.run(5000, DT_MS, rc_throttle=0.0, rc_steering=0.0)
    final = abs(rows[-1]["pitch_deg"])
    assert final == pytest.approx(SLOPE_DEG, abs=1.0)
    return next(i * DT_MS for i, row in enumerate(rows)
                if abs(row["pitch_deg"]) >= 0.63 * final)


def _false_tilt_deg(beta: float) -> float:
    """Пиковый ложный pitch/roll при разгоне в поворот на ровном."""
    with ClosedLoopSim(BIN, madgwick_beta=beta, sensor_noise=False,
                       dynamic=True) as sim:
        sim.run(SETTLE_TICKS, DT_MS, rc_throttle=0.0, rc_steering=0.0)
        rows = sim.run(4000, DT_MS, rc_throttle=0.5, rc_steering=0.6)
    return max(max(abs(r["pitch_deg"]), abs(r["roll_deg"])) for r in rows)


def test_low_beta_lags_on_slope():
    """7.7: при β = 0.05 pitch догоняет уклон заметно позже, чем при 0.2."""
    assert _slope_settle_ms(0.05) > _slope_settle_ms(0.2) + 100


def test_high_beta_tilts_on_maneuver():
    """7.7: при β = 0.2 разгон в поворот уводит наклон вдвое сильнее, чем
    при 0.05."""
    assert _false_tilt_deg(0.2) > 2.0 * _false_tilt_deg(0.05)


VIB_HZ = 40.0
VIB_DPS = 20.0
STEP_DPS = 50.0
VIB_TICKS = 1000
STEP_AT_TICK = 1500


def _gz_frames() -> list[SensorFrame]:
    """Вибрация 40 Гц первые 2 с, затем покой и шаг yaw rate на 3-й с."""
    frames = []
    for i in range(3000):
        gz = STEP_DPS if i >= STEP_AT_TICK else 0.0
        if i < VIB_TICKS:
            gz += VIB_DPS * math.sin(2.0 * math.pi * VIB_HZ * i * DT_MS / 1000)
        frames.append(SensorFrame(dt_ms=DT_MS, az=1.0, gz=gz,
                                  rc_present=True))
    return frames


def _lpf_response(cutoff_hz: float) -> tuple[float, int]:
    """(амплитуда вибрации в filtered_gz, время до 90 % шага, мс)."""
    rows = run_batch(_gz_frames(), BIN, identity_calib=True,
                     lpf_cutoff_hz=cutoff_hz)
    gz = [r["filtered_gz"] for r in rows]
    vibration = max(abs(v) for v in gz[VIB_TICKS // 2:VIB_TICKS])
    rise = next(i for i in range(STEP_AT_TICK, len(gz))
                if gz[i] >= 0.9 * STEP_DPS) - STEP_AT_TICK
    return vibration, rise * DT_MS


def test_lower_cutoff_suppresses_vibration_and_lags():
    """7.8: cutoff 10 Гц давит вибрацию 40 Гц сильнее 30 Гц, но шаг yaw
    rate доходит до 90 % позже; 60 Гц — наоборот."""
    vib_10, rise_10 = _lpf_response(10.0)
    vib_30, rise_30 = _lpf_response(30.0)
    vib_60, rise_60 = _lpf_response(60.0)
    assert vib_10 < 0.2 * vib_30
    assert vib_30 < vib_60
    assert rise_10 > rise_30 > rise_60
