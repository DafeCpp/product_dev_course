"""Модель mock-WS должна вести себя как C++ контур (test_command_path.cpp)."""

import math
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from model import BenchModel, ParseError, Scenario, parse_command  # noqa: E402


def send(m: BenchModel, **cmd) -> dict:
    m.post(cmd)
    m.step(0.04)
    return m.acks[-1]


def test_setpoint_is_ramped_and_acked():
    m = BenchModel()
    ack = send(m, cmd_id=1, type="set_setpoint", value=10.0)
    assert ack["result"] == "accepted" and not ack["duplicate"]
    m.step(0.04)
    assert 0 < m.ramped < 10.0
    for _ in range(100):
        m.step(0.04)
    assert m.ramped == pytest.approx(10.0)


def test_duplicate_cmd_id_not_reexecuted():
    m = BenchModel()
    send(m, cmd_id=1, type="set_setpoint", value=10.0)
    send(m, cmd_id=2, type="set_setpoint", value=3.0)
    dup = send(m, cmd_id=1, type="set_setpoint", value=40.0)
    assert dup["duplicate"] and dup["result"] == "accepted"
    assert m.commanded == 3.0 and m.cmd_duplicate == 1


def test_zero_id_and_out_of_range_rejected():
    m = BenchModel()
    assert send(m, cmd_id=0, type="enable", enable=True)["reason"] == "invalid_id"
    assert send(m, cmd_id=1, type="set_setpoint", value=51.0)["reason"] == "out_of_range"
    assert send(m, cmd_id=2, type="set_setpoint", value=math.nan)["reason"] == "out_of_range"


def test_program_blocks_manual_but_not_stop():
    m = BenchModel(scenario=Scenario(program=True))
    assert send(m, cmd_id=1, type="set_setpoint", value=1.0)["reason"] == "program_running"
    assert send(m, cmd_id=2, type="enable", enable=True)["reason"] == "program_running"
    assert send(m, cmd_id=3, type="controlled_stop")["result"] == "accepted"
    assert not m.program_active and m.holding


def test_operator_stop_then_resume():
    m = BenchModel()
    send(m, cmd_id=1, type="e_stop_soft")
    assert not m.enabled and m.stop_reason == "operator_e_stop_soft"
    assert send(m, cmd_id=2, type="set_setpoint", value=1.0)["reason"] == "stopped"
    assert send(m, cmd_id=3, type="enable", enable=True)["result"] == "accepted"
    assert m.enabled and not m.holding


def test_link_loss_hold_cannot_be_cleared():
    m = BenchModel(scenario=Scenario(link_loss_at_s=0.1))
    for _ in range(100):
        m.step(0.04)
    assert m.link_state == "safe_hold" and m.holding
    assert send(m, cmd_id=1, type="enable", enable=True)["reason"] == "held"


def test_slot_overwrite_counts_dropped_command():
    m = BenchModel()
    m.post({"cmd_id": 1, "type": "set_setpoint", "value": 1.0})
    m.post({"cmd_id": 2, "type": "set_setpoint", "value": 2.0})
    m.step(0.04)
    assert m.commanded == 2.0 and m.overwritten == 1 and m.cmd_total == 1


def test_snapshot_has_contract_fields():
    snap = BenchModel().snapshot()
    for key in ("v", "type", "seq", "uptime_ms", "mode", "enabled", "link_state", "holding",
                "stop_reason", "program_active", "failure_latched", "estop_chain", "setpoint",
                "valve_command", "feedback", "counters", "acks", "tick_us", "journal_total",
                "journal", "bus", "info"):
        assert key in snap, key


@pytest.mark.parametrize("obj,code", [
    ([], "syntax"),
    ({"type": "enable", "enable": True}, "missing_field"),
    ({"cmd_id": 1, "type": "enable"}, "missing_field"),
    ({"cmd_id": 1, "type": "reboot"}, "unknown_type"),
    ({"cmd_id": -1, "type": "controlled_stop"}, "bad_value"),
    ({"cmd_id": 1, "type": "controlled_stop", "ramp_ms": 5}, "unknown_key"),
])
def test_parse_command_errors(obj, code):
    with pytest.raises(ParseError) as exc:
        parse_command(obj)
    assert exc.value.code == code
