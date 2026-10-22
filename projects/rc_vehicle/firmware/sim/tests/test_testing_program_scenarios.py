"""LOS-362/LOS-367: SIL-сценарии с критериями из TESTING_PROGRAM.md.

Те же критерии PASS, что проверяются на машине, но в SIL: это регрессия и
эталон для сравнения с будущими заездами. Склон/pitch compensation, уставка
угла заноса в Drift и пресеты Kids поддержаны через ClosedLoopSim/sim_host
(LOS-367), удержание уставки заноса в Drift — LOS-384, поперечный склон и
roll compensation — LOS-386. Kids anti-spin на модели сцепления — test_kids_anti_spin.py
(LOS-370). Критерии, которые в SIL не выполняются, помечены strict xfail:
исправление сразу покажет XPASS.
"""

import dataclasses
import math

import pytest

from simlib import ClosedLoopSim, OversteerReplayConfig, find_sim_host, get_profile
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

# Режимы с yaw PID и их (уставка °/с на единицу руля, MaxCorr, минимальная
# ошибка без PID в долях уставки), см. common/drive_modes.cpp. Sport —
# LOS-371: без PID он в SIL ближе к уставке (~39 % против ~85 % в Normal).
YAW_MODES = {
    "normal": (NORMAL_STEER_TO_YAW_DPS, NORMAL_MAX_CORRECTION, 0.5),
    "sport": (120.0, 0.40, 0.3),
}


def _first_ms(rows, pred):
    for i, row in enumerate(rows):
        if pred(row):
            return (i + 1) * DT_MS
    return None


def _pulses(ms: int, thr_us: int, str_us: int) -> list[SensorFrame]:
    return [SensorFrame(dt_ms=DT_MS, rc_pulse_us=(thr_us, str_us))
            for _ in range(ms // DT_MS)]


# На ESP32 два таймаута подряд: декодер RC-импульсов (RC_IN_TIMEOUT_MS = 90 мс)
# и grace-период Failsafe (FAILSAFE_TIMEOUT_MS = 120 мс), плюс опрос RC раз в
# 20 мс; в grace-периоде прошивка держит последнюю команду (LOS-283).
# Суммарно нейтраль не позже 250 мс с запасом ~20 мс (LOS-368).
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
        assert neutral_ms >= 180
        worst = max(worst, neutral_ms)
    assert worst <= 250

    back = rows[(RC_LOSS_AT_MS + 800) // DT_MS:]
    recovered_ms = _first_ms(back, lambda r: r["failsafe"] == 0.0)
    assert recovered_ms is not None and recovered_ms <= 60
    assert back[-1]["throttle"] > 0.1


def _wifi_frames(ms: int, on: bool) -> list[SensorFrame]:
    return [SensorFrame(dt_ms=DT_MS, wifi_present=on,
                        wifi_throttle=0.3 if on else 0.0,
                        wifi_steering=0.2 if on else 0.0)
            for _ in range(ms // DT_MS)]


def test_wifi_loss_neutral_within_250_ms():
    """Failsafe по TESTING_PROGRAM.md: команды Wi-Fi пропали (RC нет) → нейтраль за ≤250 мс."""
    loss_at = 2000
    rows = run_batch(_wifi_frames(loss_at, True) + _wifi_frames(600, False), BIN)
    assert rows[loss_at // DT_MS - 1]["throttle"] > 0.1
    neutral_ms = _first_ms(
        rows[loss_at // DT_MS:],
        lambda r: r["failsafe"] == 1.0 and r["throttle"] == 0.0)
    assert neutral_ms is not None and 180 <= neutral_ms <= 250


def test_wifi_command_gap_does_not_trip_failsafe_below_timeout():
    """Web UI шлёт команды каждые 20 мс: пауза ~80 мс не должна давать failsafe, пауза ~300 мс — должна.

    Любая пауза дольше ~230 мс (таймаут команды + grace) прерывает и
    автотесты/калибровки (HandleFailsafe → StopAll), так что граница важна.
    """
    def run(gap_ms: int):
        frames = (_wifi_frames(2000, True) + _wifi_frames(gap_ms, False)
                  + _wifi_frames(1000, True))
        return run_batch(frames, BIN)[2000 // DT_MS:]

    assert all(r["failsafe"] == 0.0 for r in run(80))
    assert all(r["failsafe"] == 0.0 for r in run(160))
    assert any(r["failsafe"] == 1.0 for r in run(300))


def _yaw_step(steer: float, stabilize: bool = True, mode: str = "normal"):
    with ClosedLoopSim(BIN, dynamic=True, stabilize=stabilize,
                       sensor_noise=False, drive_mode=mode) as sim:
        sim.run(1500, rc_throttle=0.4, rc_steering=0.0)
        rows = sim.run(1000, rc_throttle=0.4, rc_steering=steer)
    yaw_dps = [math.degrees(r["ekf_yaw_rate"]) for r in rows]  # rad/s → °/с
    return rows, yaw_dps


def _mean_tail(values, n=100):
    return sum(values[-n:]) / n


# Шаги, на которых PID не упирается в ограничение коррекции: измеряется
# регулятор, а не насыщение.
YAW_STEPS = (0.3, 0.35, 0.4, 0.45, 0.5)


@pytest.mark.parametrize("mode", YAW_MODES)
@pytest.mark.parametrize("steer", YAW_STEPS)
def test_yaw_rate_pid_tracks_setpoint(steer, mode):
    """Yaw PID (Normal, Sport): средний yaw rate после шага руля у уставки, без PID — далеко."""
    steer_to_yaw, max_correction, open_loop_error = YAW_MODES[mode]
    setpoint = steer_to_yaw * steer
    rows, yaw = _yaw_step(steer, mode=mode)
    _, open_loop = _yaw_step(steer, stabilize=False, mode=mode)

    # Первые 300 мс руль ещё нарастает по slew rate; дальше разница между
    # командой и выходом — это коррекция PID.
    correction = [abs(r["steering"] - steer) for r in rows[300 // DT_MS:]]
    assert max(correction) < max_correction - 0.01  # без насыщения

    assert abs(_mean_tail(yaw) - setpoint) < 0.15 * setpoint
    assert abs(_mean_tail(open_loop) - setpoint) > open_loop_error * setpoint


@pytest.mark.parametrize("mode", YAW_MODES)
@pytest.mark.parametrize("steer", YAW_STEPS)
def test_yaw_rate_step_no_limit_cycle(steer, mode):
    """LOS-366/LOS-371: после шага yaw rate успокаивается, а не держит автоколебания."""
    setpoint = YAW_MODES[mode][0] * steer
    rows, yaw = _yaw_step(steer, mode=mode)
    tail = yaw[-100:]
    assert (max(tail) - min(tail)) / setpoint < 0.02
    steer_tail = [r["steering"] for r in rows[-100:]]
    assert max(steer_tail) - min(steer_tail) < 0.01


@pytest.mark.parametrize("mode", YAW_MODES)
@pytest.mark.parametrize("steer", YAW_STEPS)
def test_yaw_rate_step_overshoot_under_20pct(steer, mode):
    """Yaw PID по TESTING_PROGRAM.md: overshoot < 20 % от уставки."""
    setpoint = YAW_MODES[mode][0] * steer
    _, yaw = _yaw_step(steer, mode=mode)
    assert (max(yaw) - setpoint) / setpoint < 0.20


@pytest.mark.parametrize("mode", YAW_MODES)
def test_yaw_rate_step_settles_within_200_ms(mode):
    """Yaw PID по TESTING_PROGRAM.md: settling < 200 мс (полоса ±10 % от уставки)."""
    steer = 0.4
    setpoint = YAW_MODES[mode][0] * steer
    _, yaw = _yaw_step(steer, mode=mode)
    out_of_band = [i for i, y in enumerate(yaw)
                   if abs(y - setpoint) > 0.1 * setpoint]
    settling_ms = (out_of_band[-1] + 1) * DT_MS if out_of_band else 0
    assert settling_ms < 200


# Шины с пределом μ и тяга на заднюю ось: газ в пол в повороте срывает заднюю
# ось в занос. На линейных шинах без предела занос не физичен (боковое
# ускорение до 4.5 g), и guard срабатывал лишь на ложном угле от мотор-модели,
# которая выдавала скорость полного газа сразу, без разгона (LOS-378).
OVERSTEER_TIRE_MU = 0.3
OVERSTEER_FRONT_SHARE = 0.2
# Авто-калибровка IMU (1000 семплов) должна пройти в покое.
OVERSTEER_SETTLE_TICKS = 1500


def oversteer_params():
    p = get_profile("fitted_2026_08_02")
    p.tire_mu = OVERSTEER_TIRE_MU
    p.drive_front_share = OVERSTEER_FRONT_SHARE
    return p


def test_oversteer_guard_cuts_throttle_and_slip():
    """Oversteer guard: резкий газ в повороте → guard срабатывает, газ и занос снижаются."""

    def corner(reduction: float) -> tuple[list[dict], float]:
        """Строки прошивки и пик реального угла заноса модели [°]."""
        guard = OversteerReplayConfig(5.0, 10.0, reduction)
        with ClosedLoopSim(BIN, params=oversteer_params(), dynamic=True,
                           stabilize=True, sensor_noise=False,
                           oversteer=guard) as sim:
            sim.run(OVERSTEER_SETTLE_TICKS, rc_throttle=0.0, rc_steering=0.0)
            sim.run(1000, rc_throttle=0.5, rc_steering=0.0)
            rows, peak_beta = [], 0.0
            for _ in range(1500):
                rows.append(sim.step(2, 1.0, 1.0))
                st = sim.model.state
                peak_beta = max(peak_beta, abs(math.degrees(
                    math.atan2(st.vy, max(st.v, 0.1)))))
            return rows, peak_beta

    # Эталон: детектор включён, но газ не режет — занос тот же, что без guard.
    # Сравнивается реальный угол модели: в развороте угол EKF и так доходит
    # до 76–80° с guard и без, и разницу в нём не видно (LOS-378).
    detect_only, detect_only_beta = corner(0.0)
    guarded, guarded_beta = corner(0.7)
    assert any(r["oversteer_active"] == 1.0 for r in detect_only)
    assert any(r["oversteer_active"] == 1.0 for r in guarded)
    assert guarded_beta < detect_only_beta
    # Сравниваем газ по одному и тому же тику.
    assert any(g["oversteer_active"] == 1.0 and g["throttle"] < f["throttle"] - 0.1
               for f, g in zip(detect_only, guarded))


# Мотор вдвое мощнее подогнанного: на нём Kids-газ 0.3 разгоняет быстрее
# порога accel_threshold_g = 0.15 g. На штатном моторе реальное ускорение
# лишь касается порога; раньше тест держался на forward_accel, завышенном на
# ~35 % из-за vx мотор-модели, обгонявшего машину (LOS-378).
KIDS_LAUNCH_MAX_ACCEL = 16.0


def _kids_launch(**kw) -> list[dict]:
    p = get_profile("fitted_2026_08_02")
    p.max_accel = KIDS_LAUNCH_MAX_ACCEL
    with ClosedLoopSim(BIN, params=p, drive_mode="kids", sensor_noise=False,
                       **kw) as sim:
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


SLOPE_DEG = 10.0


def _climb(pitch_comp: bool, slope_deg: float = SLOPE_DEG):
    """Калибровка в покое на ровном, разгон, затем въезд на склон.

    Калибровка принимает гравитацию на старте за горизонт, поэтому склон
    включается уже после неё. Эталон тоже со stabilize=True: прогоны
    отличаются только pitch_comp.
    """
    with ClosedLoopSim(BIN, dynamic=True, sensor_noise=False, stabilize=True,
                       pitch_comp=pitch_comp) as sim:
        sim.run(1000, rc_throttle=0.0, rc_steering=0.0)
        sim.run(1000, rc_throttle=0.5, rc_steering=0.0)
        sim.set_slope(slope_deg)
        rows = sim.run(1500, rc_throttle=0.5, rc_steering=0.0)
        return rows, sim.model.state.v


def _tail_mean(rows, key, n=100):
    return _mean_tail([r[key] for r in rows], n)


def test_slope_is_seen_as_pitch():
    """LOS-367: подъём с постоянной скоростью виден Madgwick как тангаж ≈ уклон.

    Знак фиксирует текущую конвенцию прошивки: подъём даёт pitch_deg < 0
    (как и разгон); PitchCompensator это учитывает (LOS-369).
    """
    rows, _ = _climb(pitch_comp=False)
    assert abs(_tail_mean(rows, "pitch_deg") + SLOPE_DEG) < 1.5
    flat, _ = _climb(pitch_comp=False, slope_deg=0.0)
    assert abs(_tail_mean(flat, "pitch_deg")) < 1.0


def test_pitch_comp_idle_on_flat():
    """LOS-367: на ровном с постоянной скоростью компенсация газ не трогает."""
    with_comp, _ = _climb(pitch_comp=True, slope_deg=0.0)
    without, _ = _climb(pitch_comp=False, slope_deg=0.0)
    assert abs(_tail_mean(with_comp, "cmd_throttle")
               - _tail_mean(without, "cmd_throttle")) < 0.02


def test_pitch_comp_no_feedback_from_flat_acceleration():
    """LOS-369: разгон по ровному не выглядит для компенсации подъёмом.

    Madgwick на разгоне заваливается «носом вверх» (до −14° в SIL); если
    кормить им компенсацию, она добавляет газ в ответ на собственное
    ускорение. Тангаж берётся от TiltEstimator, который снимает ускорение
    мотор-модели.
    """
    def accelerate(pitch_comp: bool):
        with ClosedLoopSim(BIN, dynamic=True, sensor_noise=False,
                           stabilize=True, pitch_comp=pitch_comp) as sim:
            sim.run(1000, rc_throttle=0.0, rc_steering=0.0)
            return sim.run(3000, rc_throttle=0.5, rc_steering=0.0)

    with_comp = max(r["cmd_throttle"] for r in accelerate(True))
    without = max(r["cmd_throttle"] for r in accelerate(False))
    assert with_comp < without + 0.05


def test_pitch_comp_acts_on_slope():
    """LOS-367: --pitch-comp доходит до прошивки — на склоне газ с компенсацией
    заметно отличается от газа без неё (направление проверяют тесты ниже)."""
    with_comp, _ = _climb(pitch_comp=True)
    without, _ = _climb(pitch_comp=False)
    assert abs(_tail_mean(with_comp, "cmd_throttle")
               - _tail_mean(without, "cmd_throttle")) > 0.05


def test_pitch_comp_adds_throttle_uphill():
    """TESTING_PROGRAM.md: на подъёме pitch compensation добавляет газ и
    держит скорость выше, чем без неё."""
    with_comp, v_comp = _climb(pitch_comp=True)
    without, v_plain = _climb(pitch_comp=False)
    assert (_tail_mean(with_comp, "cmd_throttle")
            > _tail_mean(without, "cmd_throttle") + 0.05)
    assert v_comp > v_plain


def test_pitch_comp_cuts_throttle_downhill():
    """LOS-369: под гору компенсация снимает газ и машина разгоняется
    меньше, чем без неё.

    Порог ниже, чем на подъёме: TiltEstimator опирается на ускорение
    мотор-модели, которая склона не знает, и в SIL видит спуск 10° примерно
    как 5° (подъём — как 7°).
    """
    with_comp, v_comp = _climb(pitch_comp=True, slope_deg=-SLOPE_DEG)
    without, v_plain = _climb(pitch_comp=False, slope_deg=-SLOPE_DEG)
    assert (_tail_mean(with_comp, "cmd_throttle")
            < _tail_mean(without, "cmd_throttle") - 0.03)
    assert v_comp < v_plain


BANK_DEG = 5.0
ROLL_COMP_GAIN = 0.004  # штатный RollCompensationConfig::gain


def _bank_cruise(roll_comp: bool, bank_deg: float = BANK_DEG):
    """LOS-386: прямой ход по поперечному склону в Drift (без yaw-ПИД).

    Машина с недостаточной поворачиваемостью (ЦМ ближе к передней оси, у
    штатного профиля поворачиваемость нейтральная и склон её только сносит
    вбок). Калибровка на ровном, разгон, затем склон. Оба прогона со
    стабилизацией: отличаются только roll compensation. Возвращает строки на
    склоне и изменение курса модели [°] за это время.
    """
    params = dataclasses.replace(get_profile("fitted_2026_08_02"),
                                 com_a=0.12, com_b=0.18)
    with ClosedLoopSim(BIN, params=params, dynamic=True, sensor_noise=False,
                       stabilize=True, drive_mode="drift",
                       roll_comp_gain=ROLL_COMP_GAIN if roll_comp else None
                       ) as sim:
        sim.run(1000, rc_throttle=0.0, rc_steering=0.0)
        sim.run(1000, rc_throttle=0.4, rc_steering=0.0)
        psi0 = sim.model.state.psi
        sim.set_bank(bank_deg)
        rows = sim.run(2000, rc_throttle=0.4, rc_steering=0.0)
        return rows, math.degrees(sim.model.state.psi - psi0)


def test_bank_is_seen_as_roll_and_turns_car_downhill():
    """LOS-386: поперечный склон виден прошивке как крен ≈ уклону (> 0 —
    правый борт ниже), а машина без компенсации уходит вниз по склону."""
    rows, heading = _bank_cruise(roll_comp=False)
    assert abs(_tail_mean(rows, "roll_deg") - BANK_DEG) < 1.5
    assert heading < -5.0


@pytest.mark.parametrize("bank_deg", [BANK_DEG, -BANK_DEG])
def test_roll_comp_steers_uphill_and_holds_heading(bank_deg):
    """TESTING_PROGRAM.md: на поперечном склоне roll compensation рулит вверх
    по склону (руль > 0 при правом борте ниже) и уменьшает уход курса."""
    with_comp, heading_comp = _bank_cruise(roll_comp=True, bank_deg=bank_deg)
    _, heading_plain = _bank_cruise(roll_comp=False, bank_deg=bank_deg)
    steering = _tail_mean(with_comp, "steering")
    assert steering * bank_deg > 0.0
    assert abs(steering) > 0.01
    assert abs(heading_comp) < 0.7 * abs(heading_plain)


def test_roll_comp_idle_on_flat():
    """LOS-386: на ровном компенсация руль не трогает."""
    with_comp, heading_comp = _bank_cruise(roll_comp=True, bank_deg=0.0)
    without, heading_plain = _bank_cruise(roll_comp=False, bank_deg=0.0)
    assert abs(_tail_mean(with_comp, "steering")
               - _tail_mean(without, "steering")) < 1e-3
    assert abs(heading_comp - heading_plain) < 0.1


def test_drift_slip_target_shifts_throttle_correction():
    """LOS-367: уставка угла заноса (Drift) двигает коррекцию газа в сторону,
    задаваемую знаком target_deg (SlipAngleController работает по throttle)."""

    def drift(target_deg: float):
        with ClosedLoopSim(BIN, stabilize=True, drive_mode="drift",
                           dynamic=True, sensor_noise=False,
                           slip_target_deg=target_deg) as sim:
            sim.run(1500, rc_throttle=0.5, rc_steering=0.0)
            return sim.run(300, rc_throttle=0.5, rc_steering=0.3)

    positive = drift(20.0)
    negative = drift(-20.0)
    assert (_mean_tail([r["cmd_throttle"] for r in positive], n=50)
           > _mean_tail([r["cmd_throttle"] for r in negative], n=50))


DRIFT_TARGET_DEG = 15.0


def _drift_hold(steering: float) -> tuple[float, float]:
    """LOS-384: Drift на скользком (μ = 0.3), газ 0.4 и руль в поворот 5 с.

    Возвращает средний |β| модели и средний |slip| EKF за последние 2 с.
    """
    p = get_profile("fitted_2026_08_02")
    p.tire_mu = 0.3
    with ClosedLoopSim(BIN, params=p, dynamic=True, drive_mode="drift",
                       stabilize=True, sensor_noise=False,
                       slip_target_deg=DRIFT_TARGET_DEG) as sim:
        sim.run(4500, rc_throttle=0.0, rc_steering=0.0)
        sim.run(1000, rc_throttle=0.4, rc_steering=0.0)
        beta, ekf = [], []
        for _ in range(2500):
            row = sim.step(DT_MS, 0.4, steering)
            st = sim.model.state
            beta.append(abs(math.degrees(math.atan2(st.vy, max(st.v, 0.1)))))
            ekf.append(abs(row["ekf_slip_deg"]))
    return _mean_tail(beta, n=1000), _mean_tail(ekf, n=1000)


@pytest.mark.xfail(strict=True, reason=(
    "LOS-380 (баг из LOS-384): slip-PID не держит уставку. EKF в силовом заносе видит "
    "единицы градусов при β ~45°, и PID добавляет газ; в левом повороте "
    "β отрицательный, а target_deg знаковый, и ошибка уставки растёт."))
@pytest.mark.parametrize("steering", [-0.6, 0.6], ids=["right", "left"])
def test_drift_holds_slip_target(steering):
    """7.4: в повороте Drift держит |β| около target_deg (±5°)."""
    beta, _ = _drift_hold(steering)
    assert beta == pytest.approx(DRIFT_TARGET_DEG, abs=5.0)


def test_drift_scenario_slides():
    """Модель: сценарий удержания заноса и правда заносит, то есть xfail
    выше падает из-за прошивки, а не слабого сценария."""
    beta, ekf = _drift_hold(-0.6)
    assert beta > DRIFT_TARGET_DEG


KIDS_PRESET_LIMITS = {"toddler": 0.15, "child": 0.30, "preteen": 0.50}


def _kids_preset_launch(preset: str) -> list[dict]:
    with ClosedLoopSim(BIN, drive_mode="kids", kids_preset=preset,
                       sensor_noise=False, kids_accel_limit=False) as sim:
        return sim.run(1500, rc_throttle=1.0, rc_steering=0.0)


@pytest.mark.parametrize("preset", KIDS_PRESET_LIMITS)
def test_kids_preset_caps_full_stick_throttle(preset):
    """LOS-367: --kids-preset доходит до прошивки (KidsModeConfig::ApplyPreset)
    и газ в пол не выходит за лимит пресета."""
    rows = _kids_preset_launch(preset)
    limit = KIDS_PRESET_LIMITS[preset]
    assert rows[-1]["kids_throttle_limit"] == pytest.approx(limit, abs=1e-3)
    assert max(r["throttle"] for r in rows) <= limit + 1e-3


def test_kids_presets_order_by_age():
    """Старший пресет даёт больше газа при том же «газ в пол»."""
    peaks = [max(r["throttle"] for r in _kids_preset_launch(p))
             for p in ("toddler", "child", "preteen")]
    assert peaks[0] < peaks[1] < peaks[2]
