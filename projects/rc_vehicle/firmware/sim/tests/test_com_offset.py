"""LOS-384: калибровка смещения IMU от центра масс в SIL (пункт 7.6 протокола).

Модель ставит IMU в (imu_offset_x_m, imu_offset_y_m) от центра масс, и
акселерометр видит ещё ω×(ω×r) и α×r (simlib/sensors.py::lever_arm_accel).
Прошивка гоняет ComOffsetCalibration двумя кругами CW и CCW
(--start-com-calib) и должна найти заданное смещение.

Тряска дороги нужна: без неё накат по прямой проходит ZUPT, как остановка, и
второй круг начинался на ходу. Калибровка IMU — identity: у модели нулевые
bias, а в покое тряска дороги (sigma_0) не дала бы откалиброваться в покое.

Круиз 15 с, а не 5 с, как в web UI: в начале круиза машина ещё разгоняется,
это продольное ускорение в обоих кругах одного знака и в сумме CW + CCW не
сокращается. На 5 с оно сдвигает rx примерно на −0.07 м.
"""

import pytest

from simlib import ClosedLoopSim, find_sim_host, get_profile

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")

DT_MS = 2
# Старт после схождения Madgwick и EKF; калибровка IMU — identity.
CALIB_AT_MS = 9000
CRUISE_SEC = 15.0
# Два круга по 15 с, разгон, торможение до 3 с: ~42 с, с запасом.
MAX_TICKS = 30000
# Шум дороги на круге (σ ≈ 0.17 g по X) даёт разброс среднего ~0.03 м.
TOLERANCE_M = 0.04


def _calibrate(offset_m: tuple[float, float], *, seed: int = 0,
               saved_offset_m: tuple[float, float] | None = None
               ) -> tuple[float, float]:
    p = get_profile("fitted_2026_08_02")
    p.imu_offset_x_m, p.imu_offset_y_m = offset_m
    with ClosedLoopSim(BIN, params=p, dynamic=True, identity_calib=True,
                       sensor_noise=True, noise_seed=seed,
                       com_offset=saved_offset_m,
                       start_com_calib_at_ms=CALIB_AT_MS,
                       test_duration=CRUISE_SEC) as sim:
        started = False
        for _ in range(MAX_TICKS):
            row = sim.step(DT_MS)
            started = started or bool(row["com_calib_active"])
            if started and not row["com_calib_active"]:
                return row["com_offset_rx"], row["com_offset_ry"]
    raise AssertionError("калибровка CoM не завершилась")


def test_centered_imu_gives_zero_offset():
    """IMU в центре масс: калибровка не находит смещения."""
    rx, ry = _calibrate((0.0, 0.0))
    assert rx == pytest.approx(0.0, abs=TOLERANCE_M)
    assert ry == pytest.approx(0.0, abs=TOLERANCE_M)


@pytest.mark.parametrize("seed", [0, 1, 2])
def test_calibration_recovers_imu_offset(seed):
    """7.6: калибровка по кругу находит заданное смещение IMU."""
    rx, ry = _calibrate((-0.10, 0.08), seed=seed)
    assert rx == pytest.approx(-0.10, abs=TOLERANCE_M)
    assert ry == pytest.approx(0.08, abs=TOLERANCE_M)


def test_recalibration_refines_saved_offset():
    """LOS-379: повторная калибровка поверх сохранённого offset даёт полный
    offset, а не остаток."""
    rx, ry = _calibrate((-0.10, 0.08), saved_offset_m=(-0.05, 0.02))
    assert rx == pytest.approx(-0.10, abs=TOLERANCE_M)
    assert ry == pytest.approx(0.08, abs=TOLERANCE_M)
