"""LOS-362: SIL-сценарии с критериями из TESTING_PROGRAM.md.

Те же критерии PASS, что проверяются на машине, но в SIL: это регрессия и
эталон для сравнения с будущими заездами. Не покрыто (нет поддержки в модели
или sim_host): склон/pitch compensation, удержание угла заноса в Drift,
anti-spin, пресеты Kids; см. LOS-362. Критерии,
которые в SIL не выполняются, помечены strict xfail: исправление сразу
покажет XPASS.
"""

import math

import pytest

from simlib import ClosedLoopSim, OversteerReplayConfig, find_sim_host
from simlib.frame import SensorFrame
from simlib.sim_host_runner import run_batch

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")

DT_MS = 2

# Normal mode (common/drive_modes.cpp): уставка yaw rate = руль × 90 °/с,
# коррекция PID ограничена ±0.30.
NORMAL_STEER_TO_YAW_DPS = 90.0
NORMAL_MAX_CORRECTION = 0.30


def _first_ms(rows, pred):
    for i, row in enumerate(rows):
        if pred(row):
            return (i + 1) * DT_MS
    return None


def _pulses(ms: int, thr_us: int, str_us: int) -> list[SensorFrame]:
    return [SensorFrame(dt_ms=DT_MS, rc_pulse_us=(thr_us, str_us))
            for _ in range(ms // DT_MS)]


# На ESP32 два таймаута подряд: декодер RC-импульсов (RC_IN_TIMEOUT_MS = 100 мс)
# и grace-период Failsafe (FAILSAFE_TIMEOUT_MS = 130 мс), плюс опрос RC раз в
# 20 мс; в grace-периоде прошивка держит последнюю команду (LOS-283).
# Суммарно нейтраль не позже 250 мс (LOS-368).
RC_LOSS_AT_MS = 2000


def _rc_dropout_rows(loss_at_ms: int = RC_LOSS_AT_MS):
    frames = (_pulses(loss_at_ms, 1600, 1600) + _pulses(800, 0, 0)
              + _pulses(1000, 1600, 1600))
    rows = run_batch(frames, BIN)
    assert len(rows) == len(frames)
    assert rows[loss_at_ms // DT_MS - 1]["throttle"] > 0.1  # до потери едем
    lost = rows[loss_at_ms // DT_MS:(loss_at_ms + 800) // DT_MS]
    neutral_ms = _first_ms(
        lost, lambda r: r["failsafe"] == 1.0 and r["throttle"] == 0.0
        and r["steering"] == 0.0)
    assert neutral_ms is not None
    assert all(r["throttle"] == 0.0 and r["steering"] == 0.0
               for r in lost[neutral_ms // DT_MS:])
    return rows, neutral_ms


def test_rc_loss_neutral_within_250_ms_and_recovers():
    """Failsafe по TESTING_PROGRAM.md: приёмник пропал → нейтраль за ≤250 мс; импульсы вернулись → управление вернулось.

    Через сырые импульсы, чтобы в контуре был настоящий таймаут RcPulseDecoder;
    флаг rc_present=0 обошёл бы его (см. test_rc_link.py). Момент потери
    сдвигается относительно фазы опроса RC (20 мс): худший случай тоже ≤250 мс.
    """
    rows, _ = _rc_dropout_rows()
    worst = 0
    for offset in range(0, 20, DT_MS):
        _, neutral_ms = _rc_dropout_rows(RC_LOSS_AT_MS + offset)
        # Нижняя граница: нейтраль по таймаутам, а не мгновенно.
        assert neutral_ms >= 200
        worst = max(worst, neutral_ms)
    assert worst <= 250

    back = rows[(RC_LOSS_AT_MS + 800) // DT_MS:]
    recovered_ms = _first_ms(back, lambda r: r["failsafe"] == 0.0)
    assert recovered_ms is not None and recovered_ms <= 60
    assert back[-1]["throttle"] > 0.1


def test_wifi_loss_neutral_within_250_ms():
    """Failsafe по TESTING_PROGRAM.md: команды Wi-Fi пропали (RC нет) → нейтраль за ≤250 мс."""
    loss_at = 2000
    frames = [SensorFrame(dt_ms=DT_MS, wifi_present=True, wifi_throttle=0.3,
                          wifi_steering=0.2) for _ in range(loss_at // DT_MS)]
    frames += [SensorFrame(dt_ms=DT_MS) for _ in range(600 // DT_MS)]
    rows = run_batch(frames, BIN)
    assert rows[loss_at // DT_MS - 1]["throttle"] > 0.1
    neutral_ms = _first_ms(
        rows[loss_at // DT_MS:],
        lambda r: r["failsafe"] == 1.0 and r["throttle"] == 0.0)
    assert neutral_ms is not None and 200 <= neutral_ms <= 250


def _yaw_step(steer: float, stabilize: bool = True):
    with ClosedLoopSim(BIN, dynamic=True, stabilize=stabilize,
                       sensor_noise=False) as sim:
        sim.run(1500, rc_throttle=0.4, rc_steering=0.0)
        rows = sim.run(1000, rc_throttle=0.4, rc_steering=steer)
    yaw_dps = [math.degrees(r["ekf_yaw_rate"]) for r in rows]  # rad/s → °/с
    return rows, yaw_dps


def _mean_tail(values, n=100):
    return sum(values[-n:]) / n


# Шаги, на которых PID не упирается в ограничение коррекции: измеряется
# регулятор, а не насыщение. Регулятор в SIL не успокаивается, а держит
# автоколебания (период ~80 мс), поэтому «overshoot» ниже — пик этих
# колебаний относительно уставки, а не переходного процесса.
YAW_STEPS = (0.3, 0.35, 0.4, 0.45, 0.5)


@pytest.mark.parametrize("steer", YAW_STEPS)
def test_yaw_rate_pid_tracks_setpoint(steer):
    """Yaw PID (Normal): средний yaw rate после шага руля у уставки, без PID — далеко."""
    setpoint = NORMAL_STEER_TO_YAW_DPS * steer
    rows, yaw = _yaw_step(steer)
    _, open_loop = _yaw_step(steer, stabilize=False)

    # Первые 300 мс руль ещё нарастает по slew rate; дальше разница между
    # командой и выходом — это коррекция PID.
    correction = [abs(r["steering"] - steer) for r in rows[300 // DT_MS:]]
    assert max(correction) < NORMAL_MAX_CORRECTION - 0.01  # без насыщения

    assert abs(_mean_tail(yaw) - setpoint) < 0.15 * setpoint
    assert abs(_mean_tail(open_loop) - setpoint) > 0.5 * setpoint


@pytest.mark.parametrize("steer", [
    pytest.param(0.3, marks=pytest.mark.xfail(
        strict=True, reason="LOS-362: пик автоколебаний 20.5 % от уставки")),
    *YAW_STEPS[1:],
])
def test_yaw_rate_step_overshoot_under_20pct(steer):
    """Yaw PID (Normal) по TESTING_PROGRAM.md: overshoot < 20 % от уставки."""
    setpoint = NORMAL_STEER_TO_YAW_DPS * steer
    _, yaw = _yaw_step(steer)
    assert (max(yaw) - setpoint) / setpoint < 0.20


@pytest.mark.xfail(
    strict=True,
    reason="LOS-362: в SIL yaw PID в Normal держит автоколебания с периодом "
           "~80 мс (yaw rate 0..+16 % от уставки, руль 0.17..0.29) и не "
           "входит в полосу ±10 % за 200 мс")
def test_yaw_rate_step_settles_within_200_ms():
    """Yaw PID (Normal) по TESTING_PROGRAM.md: settling < 200 мс (полоса ±10 % от уставки)."""
    steer = 0.4
    setpoint = NORMAL_STEER_TO_YAW_DPS * steer
    _, yaw = _yaw_step(steer)
    out_of_band = [i for i, y in enumerate(yaw)
                   if abs(y - setpoint) > 0.1 * setpoint]
    settling_ms = (out_of_band[-1] + 1) * DT_MS if out_of_band else 0
    assert settling_ms < 200


def test_oversteer_guard_cuts_throttle_and_slip():
    """Oversteer guard: резкий газ в повороте → guard срабатывает, газ и занос снижаются."""

    def corner(reduction: float):
        guard = OversteerReplayConfig(5.0, 10.0, reduction)
        with ClosedLoopSim(BIN, dynamic=True, stabilize=True,
                           sensor_noise=False, oversteer=guard) as sim:
            sim.run(2000, rc_throttle=0.5, rc_steering=0.0)
            return sim.run(1500, rc_throttle=1.0, rc_steering=1.0)

    # Эталон: детектор включён, но газ не режет — занос тот же, что без guard.
    detect_only = corner(0.0)
    guarded = corner(0.7)
    assert any(r["oversteer_active"] == 1.0 for r in detect_only)
    assert any(r["oversteer_active"] == 1.0 for r in guarded)
    assert (max(abs(r["ekf_slip_deg"]) for r in guarded)
            < max(abs(r["ekf_slip_deg"]) for r in detect_only))
    # Сравниваем газ по одному и тому же тику.
    assert any(g["oversteer_active"] == 1.0 and g["throttle"] < f["throttle"] - 0.1
               for f, g in zip(detect_only, guarded))


def _kids_launch(**kw) -> list[dict]:
    with ClosedLoopSim(BIN, drive_mode="kids", sensor_noise=False, **kw) as sim:
        return sim.run(2000, rc_throttle=1.0, rc_steering=0.0)


def test_kids_accel_limiter_smooths_launch():
    """Kids accel limiter: газ в пол с нуля → лимитер режет газ и пик ускорения."""
    rows = _kids_launch()
    # Эталон: тот же Kids Mode, выключен только accel limiter.
    ref = _kids_launch(kids_accel_limit=False)
    assert all(r["kids_accel_limit_active"] == 0.0 for r in ref)

    assert any(r["kids_accel_limit_active"] == 1.0 for r in rows)
    assert max(abs(r["throttle"]) for r in rows) <= 0.31  # kids throttle_limit
    # Тот же тик: с лимитером газ заметно ниже (запас против ослабленного в 10 раз).
    assert any(r["kids_accel_limit_active"] == 1.0
               and r["throttle"] < f["throttle"] - 0.02
               for r, f in zip(rows, ref))
    # forward_accel в g. Порог accel_threshold_g = 0.15, но лимитер
    # пропорциональный и пик в SIL выше порога; критерий — пик ниже эталона.
    kids_peak = max(r["forward_accel"] for r in rows)
    ref_peak = max(r["forward_accel"] for r in ref)
    assert kids_peak < ref_peak - 0.012
