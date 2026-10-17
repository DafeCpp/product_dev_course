"""LOS-216: RC-вход через настоящий RcPulseDecoder в SIL.

Кадры несут сырые ширины импульсов (`SensorFrame.rc_pulse_us`), прошивка
декодирует их тем же кодом, что ISR на ESP32, и дальше идёт полный тракт:
RcInputHandler → RcLinkMonitor → failsafe → телеметрия. Serial-лог прошивки
берётся из stderr sim_host (`--log`).
"""

import re

import pytest

from simlib import find_sim_host
from simlib.frame import SensorFrame
from simlib.sim_host_runner import run_batch

BIN = find_sim_host()
pytestmark = pytest.mark.skipif(
    BIN is None, reason="sim_host не собран (cmake build tests/), см. FW-S2.1")

DT_MS = 2


def _frames(ms: int, thr_us: int, str_us: int) -> list[SensorFrame]:
    return [SensorFrame(dt_ms=DT_MS, rc_pulse_us=(thr_us, str_us))
            for _ in range(ms // DT_MS)]


def _run(frames, drive_mode="kids"):
    log: list[str] = []
    rows = run_batch(frames, BIN, drive_mode=drive_mode, log_lines=log)
    assert len(rows) == len(frames)
    return rows, log


def _rc_lines(log):
    return [ln for ln in log if "RC" in ln]


def test_stick_at_end_stop_keeps_rc_in_kids_mode():
    """Симптом LOS-216: ручка у упора (2010/990 мкс) в Kids Mode.

    До PR 2/3 такие импульсы отбрасывались, канал по таймауту считался
    потерянным и в логе весь заезд был rc_throttle = 0. Теперь вход держится
    весь заезд, значения клампятся в ±1, failsafe не срабатывает.
    """
    rows, log = _run(_frames(3000, 2010, 990))

    settled = rows[100:]  # первые ~40 мс: первый импульс + опрос RC
    assert all(r["failsafe"] == 0.0 for r in settled)
    assert all(r["rc_throttle"] == pytest.approx(1.0) for r in settled)
    assert all(r["rc_steering"] == pytest.approx(-1.0) for r in settled)
    assert all(r["kids_mode_active"] == 1.0 for r in settled)

    rc = _rc_lines(log)
    assert len(rc) == 1, rc
    assert "RC signal acquired" in rc[0]


def test_receiver_dropout_reported_with_cause_and_duration():
    """Приёмник пропал на 600 мс посреди заезда: failsafe, предупреждение в
    serial с причиной «нет импульсов» и сообщение о восстановлении."""
    frames = (_frames(2000, 1600, 1500) + _frames(600, 0, 0)
              + _frames(2000, 1600, 1500))
    rows, log = _run(frames)

    # Grace-период: декодер уже потерял сигнал, но Failsafe ещё держит
    # последнюю команду (LOS-283).
    held = rows[(2000 + 300) // DT_MS]
    assert held["failsafe"] == 0.0
    assert held["throttle"] == pytest.approx(rows[2000 // DT_MS - 1]["throttle"])
    # Во время пропадания — failsafe: таймаут декодера 250 мс, затем
    # grace-период Failsafe ещё 250 мс (LOS-362).
    dropout = rows[(2000 + 520) // DT_MS:(2000 + 600) // DT_MS]
    assert all(r["failsafe"] == 1.0 for r in dropout)
    assert all(r["throttle"] == 0.0 for r in dropout)
    assert rows[-1]["failsafe"] == 0.0
    assert rows[-1]["rc_throttle"] == pytest.approx(0.2, abs=0.01)

    rc = _rc_lines(log)
    assert len(rc) == 3, rc
    assert "RC signal acquired" in rc[0]
    assert rc[1].startswith("W ")
    assert "RC LOST #1" in rc[1]
    assert "thr: no pulses" in rc[1] and "str: no pulses" in rc[1]
    m = re.search(r"RC restored after (\d+) ms", rc[2])
    assert m, rc[2]
    # Потеря фиксируется через 250 мс после последнего импульса, возврат —
    # с первым импульсом и опросом RC (20 мс).
    assert 300 <= int(m.group(1)) <= 420


def test_out_of_range_pulses_reported_with_width():
    """Импульсы газа 2400 мкс (за окном 800..2200): канал теряется, в логе
    причина «вне окна» и ширина отброшенного импульса — отличие от обрыва
    провода видно без осциллографа."""
    frames = _frames(1500, 1500, 1500) + _frames(1500, 2400, 1500)
    rows, log = _run(frames)

    assert rows[-1]["failsafe"] == 1.0
    rc = _rc_lines(log)
    assert len(rc) == 2, rc
    assert "RC LOST #1" in rc[1]
    assert "thr: out of range 2400us" in rc[1]
    assert "str: ok" in rc[1]


def test_no_receiver_reported_once_since_boot():
    """Приёмник не подключён: одно сообщение после старта, без спама."""
    rows, log = _run(_frames(5000, 0, 0), drive_mode=None)

    assert rows[-1]["failsafe"] == 1.0
    rc = _rc_lines(log)
    assert len(rc) == 1, rc
    assert "RC: no signal since boot" in rc[0]
