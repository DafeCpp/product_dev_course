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
        # Точная дискретизация лага (устойчива при любом dt/τ, важно для fitting).
        a_cmd = self._motor_accel_cmd(throttle)
        s.accel += (a_cmd - s.accel) * (1.0 - math.exp(-dt / p.motor_tau))
        long_accel = s.accel - p.drag_coeff * s.v  # фактическое dv/dt
        s.v += long_accel * dt

        delta = self._servo_angle(steering, dt)

        # Линеаризация боковой динамики ниже выведена для движения вперёд.
        # На малой скорости она вырождается (1/v), а на реверсе меняются роли
        # управляемой/неуправляемой осей. Пока reverse-ветка не идентифицирована,
        # используем там устойчивую кинематику.
        if self.dynamic and s.v >= _DYNAMIC_MIN_SPEED:
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

        # Линейный велосипед имеет вид x_dot = A*x + B*delta, x=[vy, r].
        # Явный Euler расходится при больших dt и около нижнего порога скорости,
        # особенно во время тысяч прогонов fitting. Backward Euler требует лишь
        # решения 2x2 и A-stable для затухающей forward-динамики.
        a11 = -(p.Caf + p.Car) / (p.mass * v)
        a12 = (-p.com_a * p.Caf + p.com_b * p.Car) / (p.mass * v) - v
        a21 = (-p.com_a * p.Caf + p.com_b * p.Car) / (p.Iz * v)
        a22 = -(p.com_a**2 * p.Caf + p.com_b**2 * p.Car) / (p.Iz * v)

        q1 = s.vy + dt * p.Caf / p.mass * delta
        q2 = s.r + dt * p.com_a * p.Caf / p.Iz * delta
        m11 = 1.0 - dt * a11
        m12 = -dt * a12
        m21 = -dt * a21
        m22 = 1.0 - dt * a22
        det = m11 * m22 - m12 * m21

        vy = (q1 * m22 - m12 * q2) / det
        r = (m11 * q2 - m21 * q1) / det
        s.vy = vy
        s.r = r

        # Боковая specific force равна сумме сил шин / mass. Силы считаются по
        # состоянию в конце неявного шага, согласованно с интегратором.
        alpha_f = delta - (vy + p.com_a * r) / v
        alpha_r = -(vy - p.com_b * r) / v
        return (p.Caf * alpha_f + p.Car * alpha_r) / p.mass
