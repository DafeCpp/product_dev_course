import math

import pytest

from simlib import SimParams, VehicleModel


DT = 0.002  # 2 мс = 500 Гц


def _run(model, throttle, steering, n):
    out = None
    for _ in range(n):
        out = model.step(DT, throttle, steering)
    return out


def test_throttle_increases_speed_with_tau():
    m = VehicleModel(SimParams())
    _run(m, throttle=1.0, steering=0.0, n=500)  # 1 с
    # accel state стремится к max_accel (8 м/с²) независимо от сопротивления.
    assert m.state.accel > 7.5
    assert m.state.v > 0.0


def test_deadzone_no_motion():
    p = SimParams(motor_deadzone=0.05)
    m = VehicleModel(p)
    _run(m, throttle=0.03, steering=0.0, n=200)  # ниже мёртвой зоны
    assert abs(m.state.accel) < 1e-9
    assert abs(m.state.v) < 1e-9


def test_forward_steer_positive_yaw():
    m = VehicleModel(SimParams())
    out = _run(m, throttle=0.5, steering=0.5, n=100)
    assert out.speed > 0.0
    assert out.yaw_rate > 0.0  # delta>0, v>0 → r>0


def test_reverse_flips_yaw_sign():
    m = VehicleModel(SimParams())
    out = _run(m, throttle=-0.5, steering=0.5, n=200)
    assert out.speed < 0.0
    assert out.yaw_rate < 0.0  # тот же руль, обратный ход → обратный yaw


def test_servo_slew_rate_limited():
    p = SimParams(servo_slew_dps=600.0, servo_max_deg=25.0)
    m = VehicleModel(p)
    m.step(DT, throttle=0.0, steering=1.0)  # цель 25°, шаг ограничен slew
    max_step = p.servo_slew_dps * DT  # 1.2°
    assert abs(m.state.servo_deg) <= max_step + 1e-6


def test_servo_reaches_limit_eventually():
    p = SimParams(servo_max_deg=25.0)
    m = VehicleModel(p)
    _run(m, throttle=0.0, steering=1.0, n=500)
    assert math.isclose(m.state.servo_deg, 25.0, abs_tol=0.5)


def test_dynamic_model_runs_and_turns():
    m = VehicleModel(SimParams(), dynamic=True)
    out = _run(m, throttle=1.0, steering=0.4, n=500)
    assert out.speed > _DYN_MIN()  # разогнались выше порога динамики
    assert abs(out.yaw_rate) > 0.0
    assert math.isfinite(out.lat_accel)


def test_dynamic_model_stays_finite_on_aggressive_coarse_steps():
    """Fitting logs use ~10 ms ticks and may contain gaps clipped to 100 ms."""
    p = SimParams(Caf=49.07, Car=36.67, Iz=0.176, max_accel=4.0,
                  drag_coeff=1.0)
    m = VehicleModel(p, dynamic=True)
    for i in range(800):
        steering = 1.0 if (i // 20) % 2 == 0 else -1.0
        out = m.step(0.1, throttle=0.5, steering=steering)
        assert all(math.isfinite(v) for v in (
            m.state.vy, m.state.r, out.lat_accel, out.yaw_rate))


def test_dynamic_model_uses_kinematics_in_reverse():
    dynamic = VehicleModel(SimParams(), dynamic=True)
    kinematic = VehicleModel(SimParams(), dynamic=False)
    for _ in range(300):
        a = dynamic.step(DT, throttle=-0.5, steering=0.6)
        b = kinematic.step(DT, throttle=-0.5, steering=0.6)
    assert a == b
    assert dynamic.state.vy == 0.0


def test_slope_slows_climb_without_changing_long_accel():
    """Уклон (LOS-367) тормозит разгон через физику v, но не меняет
    long_accel — specific force остаётся актуаторной (см. sensors.py):
    из одного и того же состояния (accel, v) шаг с уклоном и без даёт
    одинаковый long_accel, но разную новую v."""
    flat = VehicleModel(SimParams())
    uphill = VehicleModel(SimParams(), slope_deg=10.0)
    flat.state.accel = uphill.state.accel = 4.0
    flat.state.v = uphill.state.v = 2.0
    out_flat = flat.step(DT, throttle=1.0, steering=0.0)
    out_uphill = uphill.step(DT, throttle=1.0, steering=0.0)
    assert math.isclose(out_uphill.long_accel, out_flat.long_accel, abs_tol=1e-6)
    assert uphill.state.v < flat.state.v


def test_slope_slows_climb_over_time():
    flat = VehicleModel(SimParams())
    uphill = VehicleModel(SimParams(), slope_deg=10.0)
    out_flat = _run(flat, throttle=1.0, steering=0.0, n=250)  # 0.5 с
    out_uphill = _run(uphill, throttle=1.0, steering=0.0, n=250)
    assert out_uphill.speed < out_flat.speed


def test_negative_slope_speeds_descent():
    flat = VehicleModel(SimParams())
    downhill = VehicleModel(SimParams(), slope_deg=-10.0)
    out_flat = _run(flat, throttle=0.3, steering=0.0, n=250)
    out_downhill = _run(downhill, throttle=0.3, steering=0.0, n=250)
    assert out_downhill.speed > out_flat.speed


def _DYN_MIN():
    from simlib.vehicle_model import _DYNAMIC_MIN_SPEED

    return _DYNAMIC_MIN_SPEED


# ── Сцепление (LOS-370) ──────────────────────────────────────────────────────


def test_default_tires_have_no_grip_limit():
    """tire_mu=0 — прежние линейные шины: колёса не буксуют."""
    m = VehicleModel(SimParams(), dynamic=True)
    out = _run(m, throttle=1.0, steering=0.0, n=500)
    assert out.slip_ratio == 0.0
    assert out.wheel_speed == out.speed


def test_low_grip_launch_spins_wheels_and_caps_accel():
    """Газ в пол на льду: корпус разгоняется не быстрее μ·g, колёса буксуют."""
    mu = 0.1
    p = SimParams(tire_mu=mu)
    m = VehicleModel(p, dynamic=True)
    peak_accel = 0.0
    peak_slip = 0.0
    for _ in range(500):
        out = m.step(DT, 1.0, 0.0)
        peak_accel = max(peak_accel, out.long_accel)
        peak_slip = max(peak_slip, out.slip_ratio)
    assert peak_accel <= mu * p.gravity_ms2 + 1e-6
    assert peak_slip > 0.3
    assert out.wheel_speed > out.speed


def test_wheels_regrip_once_body_catches_up():
    """Пробуксовка кончается: при умеренном газе колёса снова сцепляются."""
    m = VehicleModel(SimParams(tire_mu=0.3), dynamic=True)
    _run(m, throttle=1.0, steering=0.0, n=100)
    assert m.state.wheel_v > m.state.v
    out = _run(m, throttle=0.2, steering=0.0, n=2000)
    assert out.slip_ratio == 0.0
    assert out.wheel_speed == out.speed


def test_lateral_force_saturates_at_grip_limit():
    """Боковое ускорение на низком μ не превышает μ·g — и в выходе, и в
    проинтегрированной динамике (vy_dot + v·r)."""
    mu = 0.3
    p = SimParams(tire_mu=mu)
    for throttle in (0.3, 0.6, 1.0):
        m = VehicleModel(p, dynamic=True)
        _run(m, throttle=throttle, steering=0.0, n=1000)
        peak_lat = 0.0
        for i in range(1000):
            steering = 1.0 if (i // 250) % 2 == 0 else -1.0  # слалом
            vy0 = m.state.vy
            out = m.step(DT, throttle, steering)
            peak_lat = max(peak_lat, abs(out.lat_accel))
            if m.state.v >= 0.3:
                implied = (m.state.vy - vy0) / DT + m.state.v * m.state.r
                assert out.lat_accel == pytest.approx(implied, abs=1e-6)
        assert peak_lat <= mu * p.gravity_ms2 * 1.011

    linear = VehicleModel(SimParams(), dynamic=True)
    _run(linear, throttle=0.6, steering=0.0, n=1000)
    out = _run(linear, throttle=0.6, steering=1.0, n=1000)
    assert abs(out.lat_accel) > 2.0 * mu * p.gravity_ms2


def test_rear_biased_drive_slides_rear_on_low_grip():
    """Тяга на заднюю ось съедает её боковое сцепление: угол заноса растёт."""
    def peak_beta(front_share: float) -> float:
        m = VehicleModel(SimParams(tire_mu=0.2, drive_front_share=front_share),
                         dynamic=True)
        _run(m, throttle=0.5, steering=0.0, n=1000)
        peak = 0.0
        for _ in range(1000):
            m.step(DT, 0.5, 1.0)
            peak = max(peak, abs(math.degrees(
                math.atan2(m.state.vy, m.state.v))))
        return peak

    assert peak_beta(0.2) > peak_beta(0.5) + 5.0


def test_reverse_wheelspin_has_negative_slip_ratio():
    """Пробуксовка назад: колёса обгоняют корпус в сторону реверса."""
    m = VehicleModel(SimParams(tire_mu=0.1))
    peak = 0.0
    for _ in range(500):
        out = m.step(DT, -1.0, 0.0)
        peak = min(peak, out.slip_ratio)
    assert out.speed < 0.0
    assert peak < -0.2


def test_kinematic_model_respects_grip_limit():
    """Кинематика с tire_mu > 0 тоже не поворачивает круче μ·g."""
    mu = 0.1
    p = SimParams(tire_mu=mu)
    m = VehicleModel(p)
    _run(m, throttle=0.2, steering=0.0, n=1000)
    peak_lat = 0.0
    for _ in range(500):
        out = m.step(DT, 0.2, 1.0)
        peak_lat = max(peak_lat, abs(out.lat_accel))
        assert out.lat_accel == pytest.approx(m.state.v * m.state.r)
    assert 0.0 < peak_lat <= mu * p.gravity_ms2 + 1e-9


def test_axle_force_stays_within_grip_while_spinning_in_turn():
    """Пробуксовка в повороте: |(Fx, Fy)| каждой оси не больше μ·N."""
    mu = 0.2
    p = SimParams(tire_mu=mu, drive_front_share=0.2)
    m = VehicleModel(p, dynamic=True)
    _run(m, throttle=0.3, steering=0.0, n=1000)
    spin_ticks = 0
    for _ in range(1000):
        out = m.step(DT, 0.7, 1.0)
        s = m.state
        if s.v < 0.3 or out.slip_ratio <= 0.0:
            continue
        spin_ticks += 1
        alpha_f = out.delta_rad - (s.vy + p.com_a * s.r) / s.v
        alpha_r = -(s.vy - p.com_b * s.r) / s.v
        caps = m._lateral_capacity(out.long_accel)
        for n, share, alpha, c, cap in zip(
                m._axle_loads(), m._drive_shares(), (alpha_f, alpha_r),
                (p.Caf, p.Car), caps):
            fx = p.mass * out.long_accel * share
            fy = min(cap * 1.01, c * abs(alpha))
            assert math.hypot(fx, fy) <= mu * n * 1.01
    assert spin_ticks > 100
