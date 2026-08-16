"""Синтез СЫРЫХ сенсоров из состояния модели — в единицах прошивки.

accel — g, gyro — dps, mag — мГс. Ориентация IMU относительно корпуса задаётся
SimParams.imu_pitch_deg/imu_roll_deg. Точные СК-конвенции валидируются в FW-S2.6.
"""

import math
from dataclasses import dataclass

import numpy as np

from .frame import SensorFrame
from .sim_params import SimParams
from .vehicle_model import StepOutput


@dataclass(frozen=True)
class RoadExcitation:
    """Один отсчёт дорожного возбуждения в СК корпуса."""

    accel_g: tuple[float, float, float]
    gyro_dps: tuple[float, float, float]
    attitude_rad: tuple[float, float]


class _Ar2Resonator:
    """Стабильный AR(2)-резонатор с единичной стационарной дисперсией."""

    def __init__(self, rng: np.random.Generator):
        self._rng = rng
        self._y1 = 0.0
        self._y2 = 0.0

    def step(self, dt_s: float, frequency_hz: float,
             damping: float) -> tuple[float, float]:
        if dt_s <= 0.0 or frequency_hz <= 0.0:
            return 0.0, 0.0
        damping = min(max(damping, 1.0e-4), 0.9999)
        omega = 2.0 * math.pi * frequency_hz
        radius = math.exp(-damping * omega * dt_s)
        angle = omega * math.sqrt(1.0 - damping * damping) * dt_s
        a1 = 2.0 * radius * math.cos(angle)
        a2 = -(radius * radius)

        # Yule-Walker: подобрать дисперсию innovation так, чтобы Var(y)=1.
        innovation_var = (
            1.0 - a2 * a2
            - a1 * a1 * (1.0 + a2) / (1.0 - a2)
        )
        innovation = math.sqrt(max(innovation_var, 0.0)) * self._rng.normal()
        previous = self._y1
        y = a1 * previous + a2 * self._y2 + innovation
        self._y2, self._y1 = self._y1, y
        # Нормированная первая разность — coherent angular-rate для угла y.
        lag1_corr = a1 / (1.0 - a2)
        delta_sd = math.sqrt(max(2.0 * (1.0 - lag1_corr), 1.0e-12))
        return y, (y - previous) / delta_sd


class RoadNoiseModel:
    """Детерминированная скоростезависимая модель тряски дороги.

    Отдельные RNG-потоки не дают добавлению нового канала менять уже
    существующие последовательности. Новый экземпляр с тем же seed полностью
    воспроизводит прогон.
    """

    def __init__(self, params: SimParams, seed: int = 0):
        self.p = params
        streams = np.random.SeedSequence(seed).spawn(5)
        self._accel_rng = [np.random.default_rng(s) for s in streams[:3]]
        self._roll = _Ar2Resonator(np.random.default_rng(streams[3]))
        self._pitch = _Ar2Resonator(np.random.default_rng(streams[4]))

    @staticmethod
    def _sigma(base: float, slope: float, speed_ms: float) -> float:
        return max(0.0, base + slope * abs(speed_ms))

    def sample(self, speed_ms: float, dt_ms: int) -> RoadExcitation:
        p = self.p
        accel_sigmas = (
            self._sigma(p.road_ax_sigma_0_g, p.road_ax_sigma_per_ms, speed_ms),
            self._sigma(p.road_ay_sigma_0_g, p.road_ay_sigma_per_ms, speed_ms),
            self._sigma(p.road_az_sigma_0_g, p.road_az_sigma_per_ms, speed_ms),
        )
        accel = tuple(float(rng.normal(0.0, sigma))
                      for rng, sigma in zip(self._accel_rng, accel_sigmas))

        dt_s = dt_ms / 1000.0
        roll_sigma = self._sigma(
            p.road_roll_rate_sigma_0_dps,
            p.road_roll_rate_sigma_per_ms,
            speed_ms)
        pitch_sigma = self._sigma(
            p.road_pitch_rate_sigma_0_dps,
            p.road_pitch_rate_sigma_per_ms,
            speed_ms)
        roll_angle, roll_rate = self._roll.step(
            dt_s, p.road_roll_frequency_hz, p.road_attitude_damping)
        pitch_angle, pitch_rate = self._pitch.step(
            dt_s, p.road_pitch_frequency_hz, p.road_attitude_damping)
        gyro = (roll_sigma * roll_rate, pitch_sigma * pitch_rate, 0.0)
        # Для гармонического возбуждения sigma(rate) ~= omega * sigma(angle).
        def angle_rad(rate_sigma: float, frequency_hz: float,
                      normalized_angle: float) -> float:
            if frequency_hz <= 0.0:
                return 0.0
            return (math.radians(rate_sigma) / (2.0 * math.pi * frequency_hz)
                    * normalized_angle)

        attitude = (
            angle_rad(roll_sigma, p.road_roll_frequency_hz, roll_angle),
            angle_rad(pitch_sigma, p.road_pitch_frequency_hz, pitch_angle),
        )
        return RoadExcitation(
            accel, tuple(float(v) for v in gyro),
            tuple(float(v) for v in attitude))


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
                p: SimParams,
                road_accel_g: tuple[float, float, float] | None = None,
                road_attitude_rad: tuple[float, float] | None = None
                ) -> tuple[float, float, float]:
    """Акселерометр (g): гравитация (по ориентации IMU) + ускорение движения."""
    g = p.gravity_ms2
    # В СК корпуса (level): покой → (0,0,1) g; движение добавляет long/lat.
    body = np.array([long_accel / g, lat_accel / g, 1.0])
    if road_accel_g is not None:
        body += np.asarray(road_accel_g)
    if road_attitude_rad is not None:
        roll, pitch = road_attitude_rad
        cp, sp = math.cos(pitch), math.sin(pitch)
        cr, sr = math.cos(roll), math.sin(roll)
        road_rot = np.array([
            [cp, 0.0, sp],
            [sr * sp, cr, -sr * cp],
            [-cr * sp, sr, cr * cp],
        ])
        body = road_rot.T @ body
    meas = _mount_rot(p).T @ body
    return float(meas[0]), float(meas[1]), float(meas[2])


def synth_gyro(yaw_rate_rad: float,
               p: SimParams,
               road_gyro_dps: tuple[float, float, float] | None = None
               ) -> tuple[float, float, float]:
    """Гироскоп (dps): вектор (0,0,r) в СК корпуса, спроецированный по IMU."""
    r_dps = math.degrees(yaw_rate_rad)
    body = np.array([0.0, 0.0, r_dps])
    if road_gyro_dps is not None:
        body += np.asarray(road_gyro_dps)
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
               with_mag: bool = True,
               wifi_keepalive: bool = False,
               road_noise: RoadNoiseModel | None = None) -> SensorFrame:
    """Собрать SensorFrame из выхода модели (для подачи в sim_host, FW-S2.5).

    wifi_keepalive=True — нулевая WiFi-команда каждый кадр. Нужна авто-режимам
    (start_test и калибровки): они требуют неактивного пульта, но без единого
    источника команд сработал бы failsafe и PWM не выдавался бы вовсе.
    """
    excitation = road_noise.sample(out.speed, dt_ms) if road_noise else None
    ax, ay, az = synth_accel(
        out.long_accel, out.lat_accel, p,
        excitation.accel_g if excitation else None,
        excitation.attitude_rad if excitation else None)
    gx, gy, gz = synth_gyro(
        out.yaw_rate, p, excitation.gyro_dps if excitation else None)
    f = SensorFrame(dt_ms=dt_ms, ax=ax, ay=ay, az=az, gx=gx, gy=gy, gz=gz)
    if with_mag:
        f.mag_present = True
        f.mx, f.my, f.mz = synth_mag(psi_rad, p)
    if rc_throttle is not None or rc_steering is not None:
        f.rc_present = True
        f.rc_throttle = rc_throttle or 0.0
        f.rc_steering = rc_steering or 0.0
    if wifi_keepalive:
        f.wifi_present = True
        f.wifi_throttle = 0.0
        f.wifi_steering = 0.0
    return f
