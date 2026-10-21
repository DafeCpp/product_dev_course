"""Модель динамики RC-машины (кинематический + динамический велосипед).

Состояние интегрируется по шагу dt; на выходе — продольное/боковое ускорение,
yaw rate и скорость (вход для синтеза сенсоров, см. sensors.py).
"""

import itertools
import math
from dataclasses import dataclass

from .sim_params import SimParams

# Ниже этой скорости динамическая модель велосипеда вырождается (деление на v) —
# используем кинематику.
_DYNAMIC_MIN_SPEED = 0.3  # м/с

# Во сколько раз быстрее корпуса раскручиваются сорвавшиеся колёса при той же
# лишней тяге: инерция колёс и трансмиссии мала относительно массы машины
# (LOS-370). Точное значение влияет только на величину slip ratio.
_WHEEL_SPIN_GAIN = 5.0


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
    wheel_v: float = 0.0  # м/с, окружная скорость ведущих колёс (LOS-370)


@dataclass
class StepOutput:
    long_accel: float  # м/с², продольное ускорение корпуса (specific force x)
    lat_accel: float  # м/с², боковое ускорение корпуса (specific force y)
    yaw_rate: float  # рад/с
    speed: float  # м/с
    delta_rad: float  # текущий угол руля
    wheel_speed: float = 0.0  # м/с, окружная скорость колёс (LOS-370)
    # (wheel_v − v) / max(|wheel_v|, |v|): >0 — колёса обгоняют корпус вперёд
    # (пробуксовка при разгоне), <0 — отстают (юз при торможении или
    # пробуксовка назад на реверсе). 0 — катятся без проскальзывания.
    slip_ratio: float = 0.0
    yaw_accel: float = 0.0  # рад/с², для акселерометра вне центра масс (LOS-384)


class VehicleModel:
    def __init__(self, params: SimParams | None = None, dynamic: bool = False,
                 slope_deg: float = 0.0):
        """slope_deg: продольный уклон (LOS-367), >0 — подъём в гору.

        Здесь — только физика скорости (тяжесть вдоль склона); реакцию опоры
        для IMU синтезирует sensors.make_frame(slope_rad=...). Уклон можно
        менять на ходу через slope_rad.
        """
        self.p = params or SimParams()
        self.dynamic = dynamic
        self.state = VehicleState()
        self.slope_rad = math.radians(slope_deg)
        self._wheels_spinning = False
        self._spin_dir = 1.0

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
        # long_accel — specific force по X корпуса (тяга минус сопротивление):
        # именно её меряет акселерометр. Тяжесть вдоль склона меняет скорость,
        # но акселерометром не видна (свободное падение по склону).
        grade_accel = p.gravity_ms2 * math.sin(self.slope_rad)
        long_accel = self._traction_limited_accel(dt, grade_accel)
        s.v += (long_accel - grade_accel) * dt
        if not self._wheels_spinning:
            s.wheel_v = s.v

        delta = self._servo_angle(steering, dt)
        r_prev = s.r

        # Линеаризация боковой динамики ниже выведена для движения вперёд.
        # На малой скорости она вырождается (1/v), а на реверсе меняются роли
        # управляемой/неуправляемой осей. Пока reverse-ветка не идентифицирована,
        # используем там устойчивую кинематику.
        if self.dynamic and s.v >= _DYNAMIC_MIN_SPEED:
            lat_accel = self._step_dynamic(dt, delta, long_accel)
        else:
            lat_accel = self._step_kinematic(dt, delta, long_accel)

        s.psi += s.r * dt
        s.x += s.v * math.cos(s.psi) * dt
        s.y += s.v * math.sin(s.psi) * dt

        return StepOutput(
            long_accel=long_accel,
            lat_accel=lat_accel,
            yaw_rate=s.r,
            speed=s.v,
            delta_rad=delta,
            wheel_speed=s.wheel_v,
            slip_ratio=self._slip_ratio(),
            yaw_accel=(s.r - r_prev) / dt if dt > 0.0 else 0.0,
        )

    # ── Сцепление (LOS-370) ──────────────────────────────────────────────────
    def _axle_loads(self) -> tuple[float, float]:
        """Статические нормальные нагрузки осей [Н] (без переноса веса)."""
        p = self.p
        weight = p.mass * p.gravity_ms2 * math.cos(self.slope_rad)
        L = p.com_a + p.com_b
        return weight * p.com_b / L, weight * p.com_a / L

    def _drive_shares(self) -> tuple[float, float]:
        front = min(max(self.p.drive_front_share, 0.0), 1.0)
        return front, 1.0 - front

    def _traction_limit_accel(self) -> float:
        """Наибольшее ускорение привода [м/с²], которое передадут шины.

        Предел — первая ось, упёршаяся в μ·N_i при своей доле тяги.
        """
        p = self.p
        limits = [p.tire_mu * n / share
                  for n, share in zip(self._axle_loads(), self._drive_shares())
                  if share > 0.0]
        return min(limits) / p.mass if limits else 0.0

    def _traction_limited_accel(self, dt: float, grade_accel: float) -> float:
        """Продольная specific force корпуса с учётом предела сцепления.

        Сопротивление drag_coeff — противо-ЭДС мотора, оно тормозит колёса,
        поэтому шина передаёт чистую тягу accel − drag·wheel_v. Пока она в
        пределе μ·N, колёса катятся без проскальзывания (wheel_v = v). Сверх
        предела колёса срываются: корпус тянет сила трения скольжения, а
        остаток тяги раскручивает колёса, пока противо-ЭДС не уравняет её с
        трением. Сцепление возвращается, когда корпус догнал колёса к концу
        шага (скорость корпуса после интегрирования, с учётом склона).
        """
        p = self.p
        s = self.state
        if p.tire_mu <= 0.0:
            self._wheels_spinning = False
            return s.accel - p.drag_coeff * s.v
        limit = self._traction_limit_accel()
        if not self._wheels_spinning:
            demand = s.accel - p.drag_coeff * s.v
            if abs(demand) <= limit:
                return demand
            self._wheels_spinning = True
            self._spin_dir = 1.0 if demand > 0.0 else -1.0
            s.wheel_v = s.v
        friction = self._spin_dir * limit
        s.wheel_v += ((s.accel - p.drag_coeff * s.wheel_v - friction)
                      * _WHEEL_SPIN_GAIN * dt)
        v_next = s.v + (friction - grade_accel) * dt
        if (s.wheel_v - v_next) * self._spin_dir <= 0.0:
            self._wheels_spinning = False  # корпус догнал колёса — сцепление
            return max(-limit, min(limit, s.accel - p.drag_coeff * s.v))
        return friction

    def _slip_ratio(self) -> float:
        s = self.state
        ref = max(abs(s.wheel_v), abs(s.v))
        if ref < 0.1:
            return 0.0
        return (s.wheel_v - s.v) / ref

    def _lateral_capacity(self, drive_accel: float) -> tuple[float, float]:
        """Боковой предел осей [Н] по эллипсу трения sqrt((μN)² − Fx²).

        Fx оси — её доля продольной силы, переданной корпусу. При пробуксовке
        это трение скольжения на пределе, поэтому упёршаяся ось почти не
        держит вбок, а вторая сохраняет остаток эллипса. Сумма сил оси не
        выходит за μ·N, и на срыве колёс предел непрерывен.
        """
        p = self.p
        caps = []
        for n, share in zip(self._axle_loads(), self._drive_shares()):
            grip = p.tire_mu * n
            fx = p.mass * drive_accel * share
            caps.append(math.sqrt(max(grip * grip - fx * fx, 0.0)))
        return caps[0], caps[1]

    def _step_kinematic(self, dt: float, delta: float,
                        drive_accel: float = 0.0) -> float:
        p = self.p
        s = self.state
        s.r = s.v * math.tan(delta) / p.wheelbase_L
        s.vy = 0.0
        lat_accel = s.v * s.r  # центростремительное ускорение
        if p.tire_mu > 0.0:
            # Предел сцепления (LOS-370): сверх суммарной боковой силы осей
            # машина не поворачивает круче — снос (недостаточная поворачиваемость).
            cap = sum(self._lateral_capacity(drive_accel)) / p.mass
            if abs(lat_accel) > cap:
                s.r *= cap / abs(lat_accel)
                lat_accel = s.v * s.r
        return lat_accel

    def _step_dynamic(self, dt: float, delta: float,
                      drive_accel: float = 0.0) -> float:
        p = self.p
        s = self.state
        v = s.v

        if p.tire_mu <= 0.0:
            return self._solve_lateral(dt, delta, v, p.Caf, p.Car)
        return self._solve_lateral_saturated(
            dt, delta, v, self._lateral_capacity(drive_accel))

    def _solve_lateral_saturated(self, dt: float, delta: float, v: float,
                                 caps: tuple[float, float]) -> float:
        """Неявный шаг с насыщенными шинами F = clamp(C·α, ±F_max) (LOS-370).

        Сила оси кусочно-линейна по состоянию, поэтому при заданном режиме
        каждой оси (линейный / упёрся в +F_max / в −F_max) шаг backward Euler —
        линейная система 2x2. Перебираются 9 режимов, принимается тот, чей
        результат с ним согласован: решение точное, без итераций, при любом
        dt. Если согласованного режима нет (не встречалось в прогонах), берётся
        режим с наименьшей невязкой.
        """
        p = self.p
        s = self.state
        a, b = p.com_a, p.com_b
        cap_f, cap_r = caps
        vy0, r0 = s.vy, s.r
        best = None
        for mode_f, mode_r in itertools.product((0, 1, -1), repeat=2):
            # Ось в режиме 0: F = C·α; в режиме ±1: F = ±F_max (k = 0).
            kf = p.Caf if mode_f == 0 else 0.0
            kr = p.Car if mode_r == 0 else 0.0
            ff = mode_f * cap_f
            fr = mode_r * cap_r
            m11 = 1.0 + dt * (kf + kr) / (p.mass * v)
            m12 = dt * ((a * kf - b * kr) / (p.mass * v) + v)
            m21 = dt * (a * kf - b * kr) / (p.Iz * v)
            m22 = 1.0 + dt * (a * a * kf + b * b * kr) / (p.Iz * v)
            q1 = vy0 + dt * (kf * delta + ff + fr) / p.mass
            q2 = r0 + dt * (a * (kf * delta + ff) - b * fr) / p.Iz
            det = m11 * m22 - m12 * m21
            vy = (q1 * m22 - m12 * q2) / det
            r = (m11 * q2 - m21 * q1) / det
            fy_f = kf * (delta - (vy + a * r) / v) + ff
            fy_r = kr * (-(vy - b * r) / v) + fr
            # Невязка: насколько сила режима расходится с кривой шины.
            err = (abs(fy_f - _clamp(p.Caf * (delta - (vy + a * r) / v), cap_f))
                   + abs(fy_r - _clamp(p.Car * (-(vy - b * r) / v), cap_r)))
            if best is None or err < best[0]:
                best = (err, vy, r, (fy_f + fy_r) / p.mass)
            if err <= 1e-9 * (1.0 + cap_f + cap_r):
                break
        _, s.vy, s.r, lat_accel = best
        return lat_accel

    def _solve_lateral(self, dt: float, delta: float, v: float,
                       Caf: float, Car: float) -> float:
        p = self.p
        s = self.state
        # Линейный велосипед имеет вид x_dot = A*x + B*delta, x=[vy, r].
        # Явный Euler расходится при больших dt и около нижнего порога скорости,
        # особенно во время тысяч прогонов fitting. Backward Euler требует лишь
        # решения 2x2 и A-stable для затухающей forward-динамики.
        a11 = -(Caf + Car) / (p.mass * v)
        a12 = (-p.com_a * Caf + p.com_b * Car) / (p.mass * v) - v
        a21 = (-p.com_a * Caf + p.com_b * Car) / (p.Iz * v)
        a22 = -(p.com_a**2 * Caf + p.com_b**2 * Car) / (p.Iz * v)

        q1 = s.vy + dt * Caf / p.mass * delta
        q2 = s.r + dt * p.com_a * Caf / p.Iz * delta
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
        return (Caf * alpha_f + Car * alpha_r) / p.mass


def _clamp(x: float, limit: float) -> float:
    return max(-limit, min(limit, x))
