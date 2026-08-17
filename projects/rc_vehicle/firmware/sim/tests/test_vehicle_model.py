import math

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


def _DYN_MIN():
    from simlib.vehicle_model import _DYNAMIC_MIN_SPEED

    return _DYNAMIC_MIN_SPEED
