"""Модель динамики RC-машины (кинематический + динамический велосипед).

Состояние интегрируется по шагу dt; на выходе — продольное/боковое ускорение,
yaw rate и скорость (вход для синтеза сенсоров, см. sensors.py).
"""

import math
from dataclasses import dataclass

from .sim_params import SimParams

# Ниже этой скорости динамическая модель велосипеда вырождается (деление на v) —
# используем кинематику.
_DYNAMIC_MIN_SPEED = 0.3  # м/с


@dataclass
class VehicleState:
    x: float = 0.0  # м
    y: float = 0.0  # м
    psi: float = 0.0  # рад, курс
    v: float = 0.0  # м/с, продольная скорость
    vy: float = 0.0  # м/с, боковая (динамика)
    r: float = 0.0  # рад/с, yaw rate
    accel: float = 0.0  # м/с², текущее продольное ускорение после лага мотора
    servo_deg: float = 0.0  # текущий угол серво


@dataclass
class StepOutput:
    long_accel: float  # м/с², продольное ускорение корпуса (specific force x)
    lat_accel: float  # м/с², боковое ускорение корпуса (specific force y)
    yaw_rate: float  # рад/с
    speed: float  # м/с
    delta_rad: float  # текущий угол руля


class VehicleModel:
    def __init__(self, params: SimParams | None = None, dynamic: bool = False):
        self.p = params or SimParams()
        self.dynamic = dynamic
        self.state = VehicleState()

    # ── Подмодели актуаторов ─────────────────────────────────────────────────
    def _motor_accel_cmd(self, throttle: float) -> float:
        """Команда газа → целевое ускорение (мёртвая зона/breakaway, FW-R5)."""
        dz = self.p.motor_deadzone
        if abs(throttle) <= dz:
            return 0.0
        sign = 1.0 if throttle > 0.0 else -1.0
        span = max(1e-6, 1.0 - dz)
        return sign * self.p.max_accel * (abs(throttle) - dz) / span

    def _servo_angle(self, steering: float, dt: float) -> float:
        """Серво руля: лимит хода + slew rate (FW-R12)."""
        steering = max(-1.0, min(1.0, steering))
        target = self.p.servo_max_deg * steering
        max_step = self.p.servo_slew_dps * dt
        diff = target - self.state.servo_deg
        if diff > max_step:
            diff = max_step
        elif diff < -max_step:
            diff = -max_step
        self.state.servo_deg += diff
        return math.radians(self.state.servo_deg)

    # ── Шаг интегрирования ───────────────────────────────────────────────────
    def step(self, dt: float, throttle: float, steering: float) -> StepOutput:
        p = self.p
        s = self.state

        # Мотор: ускорение первого порядка + линейное сопротивление.
        a_cmd = self._motor_accel_cmd(throttle)
        s.accel += (a_cmd - s.accel) * (dt / p.motor_tau)
        long_accel = s.accel - p.drag_coeff * s.v  # фактическое dv/dt
        s.v += long_accel * dt

        delta = self._servo_angle(steering, dt)

        if self.dynamic and abs(s.v) >= _DYNAMIC_MIN_SPEED:
            lat_accel = self._step_dynamic(dt, delta)
        else:
            lat_accel = self._step_kinematic(dt, delta)

        s.psi += s.r * dt
        s.x += s.v * math.cos(s.psi) * dt
        s.y += s.v * math.sin(s.psi) * dt

        return StepOutput(
            long_accel=long_accel,
            lat_accel=lat_accel,
            yaw_rate=s.r,
            speed=s.v,
            delta_rad=delta,
        )

    def _step_kinematic(self, dt: float, delta: float) -> float:
        s = self.state
        s.r = s.v * math.tan(delta) / self.p.wheelbase_L
        s.vy = 0.0
        return s.v * s.r  # центростремительное ускорение

    def _step_dynamic(self, dt: float, delta: float) -> float:
        p = self.p
        s = self.state
        v = s.v
        # Углы увода (малые углы): перед/зад.
        alpha_f = delta - (s.vy + p.com_a * s.r) / v
        alpha_r = -(s.vy - p.com_b * s.r) / v
        fyf = p.Caf * alpha_f
        fyr = p.Car * alpha_r
        vy_dot = (fyf + fyr) / p.mass - v * s.r
        r_dot = (p.com_a * fyf - p.com_b * fyr) / p.Iz
        s.vy += vy_dot * dt
        s.r += r_dot * dt
        return vy_dot + v * s.r  # боковая specific force
