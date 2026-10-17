"""Модель контура bench-controller для mock-WS (LOS-187).

Повторяет семантику командного тракта Ф2 (LOS-185, см.
docs/experiment-control/bench-command-schema.md): single-slot очередь,
идемпотентность по cmd_id, блокировки, журнал, снимок. Физика простая:
первый порядок по позиции/усилию. Нужна, чтобы разрабатывать и показывать
страницу без железа; поведение сверяется с C++ тестами test_command_path.cpp.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass, field

LIMITS = {"force": 50_000.0, "displacement": 50.0}
RAMP = {"force": 10_000.0, "displacement": 20.0}  # ед./с
STIFFNESS_N_PER_MM = 2_000.0
SEEN_CAPACITY = 16
JOURNAL_CAPACITY = 64
JOURNAL_TAIL = 16
ACK_TAIL = 4

TYPES = {"enable", "set_mode", "set_setpoint", "controlled_stop", "e_stop_soft"}


class ParseError(Exception):
    def __init__(self, code: str):
        super().__init__(code)
        self.code = code


def parse_command(obj: object) -> dict:
    """Строгая проверка JSON-команды, как ParseCommandJson в прошивке."""
    if not isinstance(obj, dict):
        raise ParseError("syntax")
    allowed = {"cmd_id", "type", "mode", "value", "enable"}
    if set(obj) - allowed:
        raise ParseError("unknown_key")
    cmd_id = obj.get("cmd_id")
    ctype = obj.get("type")
    if cmd_id is None or ctype is None:
        raise ParseError("missing_field")
    if isinstance(cmd_id, bool) or not isinstance(cmd_id, int) or not 0 <= cmd_id <= 0xFFFFFFFF:
        raise ParseError("bad_value")
    if ctype not in TYPES:
        raise ParseError("unknown_type")
    if ctype == "enable":
        if "enable" not in obj:
            raise ParseError("missing_field")
        if not isinstance(obj["enable"], bool):
            raise ParseError("bad_value")
    if ctype == "set_mode":
        if "mode" not in obj:
            raise ParseError("missing_field")
        if obj["mode"] not in LIMITS:
            raise ParseError("unknown_type")
    if ctype == "set_setpoint":
        if "value" not in obj:
            raise ParseError("missing_field")
        v = obj["value"]
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            raise ParseError("bad_value")
    return obj


@dataclass
class Scenario:
    bus_mode: str = "bring_up_master"  # | "monitor"
    frames_seen: bool = True
    program: bool = False  # стартовать с идущей программой
    link_loss_at_s: float | None = None
    estop_chain: str = "unknown"


@dataclass
class BenchModel:
    scenario: Scenario = field(default_factory=Scenario)
    seed: int = 1

    def __post_init__(self) -> None:
        self.rng = random.Random(self.seed)
        self.t = 0.0
        self.seq = 0
        self.mode = "displacement"
        self.enabled = True
        self.holding = False
        self.hold_reason = "none"  # operator | link | failure
        self.stop_reason = "none"
        self.commanded = 0.0
        self.ramped = 0.0
        self.position = 0.0
        self.force = 0.0
        self.valve = 0.0
        self.program_t: float | None = 0.0 if self.scenario.program else None
        self.link_state = "running"
        self.link_lost_at: float | None = None
        self.slot: dict | None = None
        self.overwritten = 0
        self.cmd_total = self.cmd_rejected = self.cmd_duplicate = 0
        self.seen: list[dict] = []
        self.acks: list[dict] = []
        self.journal: list[dict] = []
        self.journal_total = 0

    # ---- вход ----
    def post(self, cmd: dict) -> None:
        if self.slot is not None:
            self.overwritten += 1
        self.slot = cmd

    # ---- журнал ----
    def _log(self, kind: str, **fields: object) -> None:
        entry = {"seq": self.journal_total, "t_ms": int(self.t * 1000), "kind": kind, **fields}
        self.journal.append(entry)
        del self.journal[:-JOURNAL_CAPACITY]
        self.journal_total += 1

    # ---- исполнение ----
    @property
    def program_active(self) -> bool:
        return self.program_t is not None

    def _enter_hold(self, reason: str, stop_reason: str) -> None:
        if self.holding:
            if self.hold_reason == "operator" and reason != "operator":
                self.hold_reason, self.stop_reason = reason, stop_reason
                self._log("hold", reason=stop_reason)
            return
        self.holding, self.hold_reason, self.stop_reason = True, reason, stop_reason
        self.mode = "displacement"
        self.commanded = self.ramped = self.position
        self._log("hold", reason=stop_reason)

    def _execute(self, cmd: dict) -> dict:
        ack = {"cmd_id": cmd["cmd_id"], "cmd": cmd["type"], "result": "accepted",
               "reason": "none", "duplicate": False}

        def reject(reason: str) -> dict:
            ack.update(result="rejected", reason=reason)
            return ack

        if cmd["cmd_id"] == 0:
            return reject("invalid_id")
        ctype = cmd["type"]
        if ctype in ("controlled_stop", "e_stop_soft"):
            self.program_t = None
            if ctype == "e_stop_soft":
                self.enabled = False
            self._enter_hold("operator",
                             "operator_controlled_stop" if ctype == "controlled_stop"
                             else "operator_e_stop_soft")
            return ack
        if ctype == "enable":
            if not cmd["enable"]:
                self.enabled = False
                return ack
            if self.holding and self.hold_reason != "operator":
                return reject("held")
            if self.program_active:
                return reject("program_running")
            if self.holding:
                self.holding, self.hold_reason = False, "none"
                self.commanded = self.ramped = self.position
            self.enabled = True
            return ack
        # set_mode / set_setpoint
        if self.holding:
            return reject("stopped" if self.hold_reason == "operator" else "held")
        if self.program_active:
            return reject("program_running")
        if ctype == "set_mode":
            if cmd["mode"] != self.mode:
                self.mode = cmd["mode"]
                self.commanded = self.ramped = self.force if self.mode == "force" else self.position
            return ack
        value = float(cmd["value"])
        if not math.isfinite(value) or abs(value) > LIMITS[self.mode]:
            return reject("out_of_range")
        self.commanded = value
        return ack

    def _process(self, cmd: dict) -> None:
        self.cmd_total += 1
        duplicate = next((s for s in self.seen if s["cmd_id"] == cmd["cmd_id"]), None) \
            if cmd["cmd_id"] != 0 else None
        if duplicate:
            ack = {**duplicate, "duplicate": True}
            self.cmd_duplicate += 1
        else:
            ack = self._execute(cmd)
            if ack["reason"] != "invalid_id":
                self.seen.append(dict(ack))
                del self.seen[:-SEEN_CAPACITY]
            if ack["result"] == "rejected":
                self.cmd_rejected += 1
        self.acks.append(ack)
        del self.acks[:-ACK_TAIL]
        self._log("command", cmd_id=cmd["cmd_id"], cmd=ack["cmd"], result=ack["result"],
                  reason=ack["reason"], duplicate=ack["duplicate"])

    # ---- шаг ----
    def step(self, dt: float) -> None:
        self.t += dt
        if self.slot is not None:
            cmd, self.slot = self.slot, None
            self._process(cmd)

        loss = self.scenario.link_loss_at_s
        if loss is not None and self.t >= loss:
            if self.link_lost_at is None:
                self.link_lost_at = self.t
            lost_for = self.t - self.link_lost_at
            new = "grace" if lost_for < 0.5 else "ramp_down" if lost_for < 2.5 else "safe_hold"
            if new != self.link_state:
                self._log("link_state", **{"from": self.link_state, "to": new})
                self.link_state = new
            if new == "safe_hold":
                self.program_t = None
                self._enter_hold("link", "link_safe_hold")

        if self.program_t is not None:
            self.program_t += dt
            self.mode = "force"
            self.ramped = 10_000.0 + 5_000.0 * math.sin(2 * math.pi * 1.0 * self.program_t)
            if self.program_t > 30.0:
                self.program_t = None
                self.commanded = self.ramped = 10_000.0
        elif not self.holding:
            step = RAMP[self.mode] * dt
            self.ramped += max(-step, min(step, self.commanded - self.ramped))

        target = self.position if self.holding else self.ramped
        tau = 0.15
        if self.mode == "force":
            self.force += (target - self.force) * min(1.0, dt / tau)
            self.position = self.force / STIFFNESS_N_PER_MM
        else:
            self.position += (target - self.position) * min(1.0, dt / tau)
            self.force = self.position * STIFFNESS_N_PER_MM
        measured = self.force if self.mode == "force" else self.position
        scale = LIMITS[self.mode]
        self.valve = max(-1.0, min(1.0, (target - measured) / scale * 5.0)) if self.enabled else 0.0

    # ---- снимок (схема v1 + расширения bus/info) ----
    def snapshot(self) -> dict:
        self.seq += 1
        jitter = self.rng.randint(0, 40)
        bus_ok = self.scenario.frames_seen
        return {
            "v": 1, "type": "snapshot", "seq": self.seq, "uptime_ms": int(self.t * 1000),
            "mode": self.mode, "enabled": self.enabled, "link_state": self.link_state,
            "holding": self.holding, "stop_reason": self.stop_reason,
            "program_active": self.program_active, "failure_latched": False,
            "estop_chain": self.scenario.estop_chain,
            "setpoint": {
                "program": self.ramped if self.program_active else 0.0,
                "commanded": self.commanded, "ramped": self.ramped, "effective": self.ramped,
            },
            "valve_command": self.valve,
            "feedback": {
                "force_n": self.force, "position_mm": self.position, "status": 0,
                "fresh": bus_ok, "age_ticks": 0 if bus_ok else 500,
            },
            "counters": {
                "cmd_total": self.cmd_total, "cmd_rejected": self.cmd_rejected,
                "cmd_duplicate": self.cmd_duplicate, "cmd_overwritten": self.overwritten,
            },
            "acks": list(self.acks),
            "tick_us": {"count": int(self.t * 500), "min": 1990, "avg": 2000,
                        "p99": 2010 + jitter, "max": 2050 + jitter * 2},
            "journal_total": self.journal_total,
            "journal": self.journal[-JOURNAL_TAIL:],
            "bus": {
                "mode": self.scenario.bus_mode, "frames_seen": bus_ok,
                "nodes": [{"id": 1, "nmt": "operational" if bus_ok else "unknown",
                           "hb_age_ms": 120 if bus_ok else None, "hb_fresh": bus_ok}],
            },
            "info": {"fw": "bench-head mock 0.1", "config": "mock-cfg-1"},
        }
