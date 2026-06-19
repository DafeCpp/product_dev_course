"""Синтез СЫРЫХ сенсоров из состояния модели — в единицах прошивки.

accel — g, gyro — dps, mag — мГс. Ориентация IMU относительно корпуса задаётся
SimParams.imu_pitch_deg/imu_roll_deg. Точные СК-конвенции валидируются в FW-S2.6.
"""

import math

import numpy as np

from .frame import SensorFrame
from .sim_params import SimParams
from .vehicle_model import StepOutput


def _mount_rot(p: SimParams) -> np.ndarray:
    """Матрица ориентации IMU (pitch вокруг Y, roll вокруг X)."""
    cp, sp = math.cos(math.radians(p.imu_pitch_deg)), math.sin(
        math.radians(p.imu_pitch_deg))
    cr, sr = math.cos(math.radians(p.imu_roll_deg)), math.sin(
        math.radians(p.imu_roll_deg))
    ry = np.array([[cp, 0.0, sp], [0.0, 1.0, 0.0], [-sp, 0.0, cp]])
    rx = np.array([[1.0, 0.0, 0.0], [0.0, cr, -sr], [0.0, sr, cr]])
    return rx @ ry


def synth_accel(long_accel: float, lat_accel: float,
                p: SimParams) -> tuple[float, float, float]:
    """Акселерометр (g): гравитация (по ориентации IMU) + ускорение движения."""
    g = p.gravity_ms2
    # В СК корпуса (level): покой → (0,0,1) g; движение добавляет long/lat.
    body = np.array([long_accel / g, lat_accel / g, 1.0])
    meas = _mount_rot(p).T @ body
    return float(meas[0]), float(meas[1]), float(meas[2])


def synth_gyro(yaw_rate_rad: float,
               p: SimParams) -> tuple[float, float, float]:
    """Гироскоп (dps): вектор (0,0,r) в СК корпуса, спроецированный по IMU."""
    r_dps = math.degrees(yaw_rate_rad)
    body = np.array([0.0, 0.0, r_dps])
    meas = _mount_rot(p).T @ body
    return (
        float(meas[0]) + p.gyro_bias_dps * 0.0,
        float(meas[1]),
        float(meas[2]) + p.gyro_bias_dps,
    )


def synth_mag(psi_rad: float, p: SimParams) -> tuple[float, float, float]:
    """Магнитометр (мГс): горизонтальное поле Земли (North) в СК корпуса."""
    f = p.mag_field_mga
    # Поле в мире: North = +X. Поворот корпуса по курсу psi (вокруг Z).
    cz, sz = math.cos(psi_rad), math.sin(psi_rad)
    rz_t = np.array([[cz, sz, 0.0], [-sz, cz, 0.0], [0.0, 0.0, 1.0]])
    world = np.array([f, 0.0, 0.0])
    meas = _mount_rot(p).T @ (rz_t @ world)
    return float(meas[0]), float(meas[1]), float(meas[2])


def make_frame(out: StepOutput, psi_rad: float, p: SimParams, dt_ms: int = 2,
               rc_throttle: float | None = None,
               rc_steering: float | None = None,
               with_mag: bool = True) -> SensorFrame:
    """Собрать SensorFrame из выхода модели (для подачи в sim_host, FW-S2.5)."""
    ax, ay, az = synth_accel(out.long_accel, out.lat_accel, p)
    gx, gy, gz = synth_gyro(out.yaw_rate, p)
    f = SensorFrame(dt_ms=dt_ms, ax=ax, ay=ay, az=az, gx=gx, gy=gy, gz=gz)
    if with_mag:
        f.mag_present = True
        f.mx, f.my, f.mz = synth_mag(psi_rad, p)
    if rc_throttle is not None or rc_steering is not None:
        f.rc_present = True
        f.rc_throttle = rc_throttle or 0.0
        f.rc_steering = rc_steering or 0.0
    return f
