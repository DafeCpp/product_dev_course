"""LOS-370: Kids anti-spin в closed-loop SIL на модели сцепления.

Модель шин с пределом μ (SimParams.tire_mu, simlib/vehicle_model.py) даёт
пробуксовку колёс при резком газе и занос на скользком покрытии. Здесь
проверяется, срабатывает ли anti-spin детского режима и уменьшает ли он
пробуксовку и занос относительно эталона без него (--no-kids-anti-spin).

Что сейчас не так:
- при пробуксовке по прямой угла заноса нет вовсе, и anti-spin молчит
  (LOS-385);
- угол заноса EKF в повороте занижен: боковая скорость vy затухает с
  vy_decay_hz = 5 Гц, а снизить затухание нельзя, пока мотор-модель
  ошибается в vx на 10–20 % — ошибка vx·r даёт ложный занос (LOS-378);
- даже сработав, резкий срез газа anti-spin тормозит заднюю ось
  противо-ЭДС, и машину разворачивает (LOS-389).
Такие критерии помечены strict xfail: исправление сразу покажет XPASS.
Отдельные тесты проверяют, что модель действительно буксует и заносит, то
есть xfail падает по названной причине, а не из-за слабого сценария.
"""

import math

import pytest

from simlib import ClosedLoopSim, find_sim_host
from simlib.profiles import get_profile

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")

DT_MS = 2
# Авто-калибровка IMU при старте — 1000 семплов (2 с, CalibrationManager::
# StartAutoCalibration) плюс сход фильтров. Меньше — и калибровка захватывала
# разгон: gravity_vec наклонялся на ~2°, и в поворот yaw rate утекал в gx.
SETTLE_TICKS = 1500
# Kids по умолчанию (common/stabilization_config.hpp, KidsModeConfig).
ANTI_SPIN_THRESHOLD_DEG = 10.0

# Лёд: тяга Kids (газ ≤ 0.3) на старте превышает μ·g — колёса срываются.
ICE_MU = 0.1
# Скользкое покрытие + тяга смещена назад: под газом задняя ось теряет
# боковое сцепление раньше передней (занос), а не уходит в снос.
SLIPPERY_MU = 0.2
REAR_BIASED_FRONT_SHARE = 0.2


def _run(*, mu: float, anti_spin: bool, front_share: float = 0.5,
         straight_ticks: int, turn_ticks: int = 0) -> dict:
    """Газ в пол; после straight_ticks — полный руль на turn_ticks."""
    p = get_profile("fitted_2026_08_02")
    p.tire_mu = mu
    p.drive_front_share = front_share
    with ClosedLoopSim(BIN, params=p, dynamic=True, drive_mode="kids",
                       kids_anti_spin=anti_spin, sensor_noise=False) as sim:
        sim.run(SETTLE_TICKS, DT_MS, rc_throttle=0.0, rc_steering=0.0)
        slip_ratio, beta, ekf_slip, active = [], [], [], 0
        beta_at_trigger = None
        for i in range(straight_ticks + turn_ticks):
            row = sim.step(DT_MS, 1.0, 0.0 if i < straight_ticks else 1.0)
            st = sim.model.state
            slip_ratio.append(sim.last_out.slip_ratio)
            beta.append(abs(math.degrees(math.atan2(st.vy, max(st.v, 0.1)))))
            ekf_slip.append(abs(row["ekf_slip_deg"]))
            active += int(row["kids_anti_spin_active"])
            if active and beta_at_trigger is None:
                beta_at_trigger = beta[-1]
    return {
        "peak_slip_ratio": max(slip_ratio),
        "spin_ticks": sum(s > 0.05 for s in slip_ratio),
        "peak_beta_deg": max(beta),
        "peak_ekf_slip_deg": max(ekf_slip),
        "anti_spin_ticks": active,
        "beta_at_trigger_deg": beta_at_trigger,
    }


def _ice_launch(anti_spin: bool) -> dict:
    return _run(mu=ICE_MU, anti_spin=anti_spin, straight_ticks=1500)


def _slippery_turn(anti_spin: bool) -> dict:
    return _run(mu=SLIPPERY_MU, anti_spin=anti_spin,
                front_share=REAR_BIASED_FRONT_SHARE,
                straight_ticks=1000, turn_ticks=1500)


def test_ice_launch_spins_wheels():
    """Модель: газ в пол на льду срывает колёса в пробуксовку."""
    ref = _ice_launch(anti_spin=False)
    assert ref["peak_slip_ratio"] > 0.2
    assert ref["spin_ticks"] > 100  # > 0.2 с


@pytest.mark.xfail(strict=True, reason=(
    "LOS-370: anti-spin смотрит только на угол заноса EKF, а при "
    "пробуксовке по прямой его нет — защита не срабатывает"))
def test_anti_spin_cuts_wheelspin_on_ice_launch():
    ref = _ice_launch(anti_spin=False)
    kids = _ice_launch(anti_spin=True)
    assert kids["anti_spin_ticks"] > 0
    assert kids["peak_slip_ratio"] < 0.8 * ref["peak_slip_ratio"]


def test_slippery_turn_slides_past_anti_spin_threshold():
    """Модель: на скользком под газом машину заносит сильнее порога."""
    ref = _slippery_turn(anti_spin=False)
    assert ref["peak_beta_deg"] > 2.0 * ANTI_SPIN_THRESHOLD_DEG


@pytest.mark.xfail(strict=True, reason=(
    "LOS-378: vy в EKF затухает с vy_decay_hz = 5 Гц; снизить нельзя, пока "
    "ошибка vx мотор-модели (10–20 %) даёт ложный занос на асфальте"))
def test_anti_spin_triggers_early_on_slippery_turn():
    """LOS-378: угол заноса EKF доходит до порога раньше, чем β до 20°."""
    kids = _slippery_turn(anti_spin=True)
    assert kids["anti_spin_ticks"] > 0
    assert kids["beta_at_trigger_deg"] < 2.0 * ANTI_SPIN_THRESHOLD_DEG


@pytest.mark.xfail(strict=True, reason=(
    "LOS-378: vy в EKF затухает с vy_decay_hz = 5 Гц; снизить нельзя, пока "
    "ошибка vx мотор-модели (10–20 %) даёт ложный занос на асфальте"))
def test_ekf_slip_tracks_slide_without_anti_spin():
    """LOS-378: в заносе ~27° угол EKF не застревает на единицах градусов."""
    ref = _slippery_turn(anti_spin=False)
    assert ref["peak_ekf_slip_deg"] > 0.5 * ref["peak_beta_deg"]


@pytest.mark.xfail(strict=True, reason=(
    "LOS-389: срез газа anti-spin (0.30 → 0.09) тормозит заднюю ось "
    "противо-ЭДС, она теряет боковое сцепление, и машину разворачивает"))
def test_anti_spin_reduces_slide_on_slippery_turn():
    ref = _slippery_turn(anti_spin=False)
    kids = _slippery_turn(anti_spin=True)
    assert kids["anti_spin_ticks"] > 0
    assert kids["peak_beta_deg"] < 0.8 * ref["peak_beta_deg"]
