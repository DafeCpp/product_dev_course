"""LOS-370: Kids anti-spin в closed-loop SIL на модели сцепления.

Модель шин с пределом μ (SimParams.tire_mu, simlib/vehicle_model.py) даёт
пробуксовку колёс при резком газе и занос на скользком покрытии. Здесь
проверяется, срабатывает ли anti-spin детского режима и уменьшает ли он
пробуксовку и занос относительно эталона без него (--no-kids-anti-spin).

При пробуксовке по прямой угла заноса нет вовсе, поэтому anti-spin
включается ещё и по пробуксовке колёс: мотор-модель против интеграла
продольного ускорения IMU (LOS-385). На асфальте этот признак молчать обязан.

Угол заноса EKF в медленном заносе держится за счёт признака потери
сцепления: yaw rate разошёлся с рулём, и EKF перестаёт гасить боковую
скорость (LOS-378). Пока шины держат, vy гасится с 5 Гц, иначе тряска дороги
в акселерометре (≈0.43 g на 2 м/с) давала бы ложный занос на асфальте.

Что сейчас не так: в медленном заносе anti-spin срабатывает вовремя, но газ
наката, ниже которого он не режет (LOS-389), на 2 м/с — те же 0.29, и занос
не уменьшается.
Срез газа anti-spin не опускается ниже газа наката (LOS-389): раньше резкий
срез 0.30 → 0.09 тормозил заднюю ось противо-ЭДС, и занос переходил в
разворот. Это проверяется на быстром заносе, где anti-spin срабатывает.
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
# После калибровки TiltEstimator сходится 3τ = 6 с, и до этого детектор
# пробуксовки молчит (LOS-385), отсюда 9 с.
SETTLE_TICKS = 4500
# Kids по умолчанию (common/stabilization_config.hpp, KidsModeConfig).
ANTI_SPIN_THRESHOLD_DEG = 10.0

# Лёд: тяга Kids (газ ≤ 0.3) на старте превышает μ·g — колёса срываются.
ICE_MU = 0.1
# Скользкое покрытие + тяга смещена назад: под газом задняя ось теряет
# боковое сцепление раньше передней (занос), а не уходит в снос.
SLIPPERY_MU = 0.2
REAR_BIASED_FRONT_SHARE = 0.2


def _run(*, mu: float, anti_spin: bool, front_share: float = 0.5,
         straight_ticks: int, turn_ticks: int = 0,
         sensor_noise: bool = False) -> dict:
    """Газ в пол; после straight_ticks — полный руль на turn_ticks."""
    p = get_profile("fitted_2026_08_02")
    p.tire_mu = mu
    p.drive_front_share = front_share
    with ClosedLoopSim(BIN, params=p, dynamic=True, drive_mode="kids",
                       kids_anti_spin=anti_spin,
                       sensor_noise=sensor_noise) as sim:
        sim.run(SETTLE_TICKS, DT_MS, rc_throttle=0.0, rc_steering=0.0)
        slip_ratio, beta, ekf_slip, active = [], [], [], 0
        wheelspin = 0
        beta_at_trigger = None
        for i in range(straight_ticks + turn_ticks):
            row = sim.step(DT_MS, 1.0, 0.0 if i < straight_ticks else 1.0)
            st = sim.model.state
            slip_ratio.append(sim.last_out.slip_ratio)
            beta.append(abs(math.degrees(math.atan2(st.vy, max(st.v, 0.1)))))
            ekf_slip.append(abs(row["ekf_slip_deg"]))
            active += int(row["kids_anti_spin_active"])
            wheelspin += int(row["kids_wheelspin_active"])
            if active and beta_at_trigger is None:
                beta_at_trigger = beta[-1]
    return {
        "peak_slip_ratio": max(slip_ratio),
        "spin_ticks": sum(s > 0.05 for s in slip_ratio),
        "peak_beta_deg": max(beta),
        "peak_ekf_slip_deg": max(ekf_slip),
        "anti_spin_ticks": active,
        "wheelspin_ticks": wheelspin,
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


def test_anti_spin_cuts_wheelspin_on_ice_launch():
    """LOS-385: пробуксовку на старте ловит признак колёс, не угол заноса."""
    ref = _ice_launch(anti_spin=False)
    kids = _ice_launch(anti_spin=True)
    assert kids["wheelspin_ticks"] > 0
    assert kids["peak_slip_ratio"] < 0.8 * ref["peak_slip_ratio"]


@pytest.mark.parametrize("turn_ticks", [0, 2500])
def test_no_wheelspin_on_asphalt(turn_ticks):
    """LOS-385: на асфальте признак пробуксовки молчит — по прямой и в повороте."""
    kids = _run(mu=0.0, anti_spin=True, straight_ticks=1000,
                turn_ticks=turn_ticks, sensor_noise=True)
    assert kids["wheelspin_ticks"] == 0


@pytest.mark.parametrize("seed", [0, 1, 2])
def test_no_false_slip_on_asphalt(seed):
    """LOS-378: в повороте на асфальте с тряской дороги угол EKF ниже порога.

    Шины держат, признак потери сцепления молчит, и vy гасится с 5 Гц.
    """
    p = get_profile("fitted_2026_08_02")
    with ClosedLoopSim(BIN, params=p, dynamic=True, drive_mode="kids",
                       sensor_noise=True, noise_seed=seed) as sim:
        sim.run(SETTLE_TICKS, DT_MS, rc_throttle=0.0, rc_steering=0.0)
        rows = [sim.step(DT_MS, 1.0, 0.0 if i < 1000 else 1.0)
                for i in range(4500)]
    assert max(abs(r["ekf_slip_deg"]) for r in rows) < ANTI_SPIN_THRESHOLD_DEG
    assert sum(r["kids_anti_spin_active"] for r in rows) == 0


def test_slippery_turn_slides_past_anti_spin_threshold():
    """Модель: на скользком под газом машину заносит сильнее порога."""
    ref = _slippery_turn(anti_spin=False)
    assert ref["peak_beta_deg"] > 2.0 * ANTI_SPIN_THRESHOLD_DEG


def test_anti_spin_triggers_early_on_slippery_turn():
    """LOS-378: угол заноса EKF доходит до порога раньше, чем β до 20°."""
    kids = _slippery_turn(anti_spin=True)
    assert kids["anti_spin_ticks"] > 0
    assert kids["beta_at_trigger_deg"] < 2.0 * ANTI_SPIN_THRESHOLD_DEG


def test_ekf_slip_tracks_slide_without_anti_spin():
    """LOS-378: в заносе ~27° угол EKF не застревает на единицах градусов."""
    ref = _slippery_turn(anti_spin=False)
    assert ref["peak_ekf_slip_deg"] > 0.5 * ref["peak_beta_deg"]


def _fast_spin(anti_spin: bool) -> dict:
    """Лёд и тяга назад: занос быстрый, EKF его видит, и anti-spin срабатывает.

    Руль зажат до упора, поэтому разворота не избежать и с anti-spin; критерий
    — anti-spin его не усиливает.
    """
    return _run(mu=ICE_MU, anti_spin=anti_spin,
                front_share=REAR_BIASED_FRONT_SHARE,
                straight_ticks=1000, turn_ticks=1500)


def test_anti_spin_does_not_worsen_fast_spin():
    """LOS-389: срез газа не ниже наката не усиливает разворот."""
    ref = _fast_spin(anti_spin=False)
    kids = _fast_spin(anti_spin=True)
    assert kids["anti_spin_ticks"] > 0
    assert kids["peak_beta_deg"] < ref["peak_beta_deg"]


@pytest.mark.xfail(strict=True, reason=(
    "anti-spin срабатывает при β ≈ 9°, но пол наката (LOS-389) на 2 м/с "
    "держит газ 0.29 из 0.30, и занос не уменьшается"))
def test_anti_spin_reduces_slide_on_slippery_turn():
    ref = _slippery_turn(anti_spin=False)
    kids = _slippery_turn(anti_spin=True)
    assert kids["anti_spin_ticks"] > 0
    assert kids["peak_beta_deg"] < 0.8 * ref["peak_beta_deg"]
