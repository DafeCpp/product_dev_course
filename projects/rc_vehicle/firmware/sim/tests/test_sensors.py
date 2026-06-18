import math

from simlib import SimParams, StepOutput, make_frame, synth_accel, synth_gyro, synth_mag


def test_level_rest_accel_is_gravity_z():
    ax, ay, az = synth_accel(0.0, 0.0, SimParams())
    assert abs(ax) < 1e-6
    assert abs(ay) < 1e-6
    assert math.isclose(az, 1.0, abs_tol=1e-6)


def test_forward_accel_appears_in_ax():
    p = SimParams()
    ax, _, _ = synth_accel(p.gravity_ms2 * 0.5, 0.0, p)  # 0.5 g вперёд
    assert math.isclose(ax, 0.5, abs_tol=1e-6)


def test_pitch_mount_leaks_gravity_into_ax():
    p = SimParams(imu_pitch_deg=10.0)
    ax, _, az = synth_accel(0.0, 0.0, p)
    assert ax < 0.0  # ax = -sin(pitch)
    assert math.isclose(ax, -math.sin(math.radians(10.0)), abs_tol=1e-6)
    assert az < 1.0


def test_gyro_z_matches_yaw_rate():
    p = SimParams()
    r = math.radians(90.0)  # 90 dps
    gx, gy, gz = synth_gyro(r, p)
    assert math.isclose(gz, 90.0, abs_tol=1e-6)
    assert abs(gx) < 1e-6 and abs(gy) < 1e-6


def test_mag_magnitude_preserved_and_heading_varies():
    p = SimParams()
    m0 = synth_mag(0.0, p)
    m90 = synth_mag(math.radians(90.0), p)
    norm = lambda v: math.sqrt(sum(c * c for c in v))
    assert math.isclose(norm(m0), p.mag_field_mga, rel_tol=1e-6)
    assert math.isclose(norm(m90), p.mag_field_mga, rel_tol=1e-6)
    assert not math.isclose(m0[0], m90[0], abs_tol=1.0)  # курс меняет проекцию


def test_make_frame_csv_has_17_fields():
    out = StepOutput(long_accel=1.0, lat_accel=0.2, yaw_rate=0.1, speed=2.0,
                     delta_rad=0.05)
    f = make_frame(out, psi_rad=0.3, p=SimParams(), dt_ms=2,
                   rc_throttle=0.5, rc_steering=-0.2)
    parts = f.to_csv().split(",")
    assert len(parts) == 17
    assert parts[0] == "2"  # dt_ms
    assert f.mag_present and f.rc_present
