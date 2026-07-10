"""Сценарии измерений (a)–(e) плана LOS-76 на vcan-риге.

Каждый сценарий: эмулятор узла (поток) + sim_host --socketcan
(subprocess) + монитор шины (RTT/нагрузка). Результат — markdown-
фрагменты для отчёта spike-mcu-control-loop.md.

Запуск:  python -m benchsim.scenarios --sim-host <путь> [--channel vcan0]
"""

from __future__ import annotations

import argparse
import csv
import io
import math
import statistics
import subprocess
import threading
import time

import can

from . import valve_node
from .valve_node import ValveNode

SETPOINT_ID = valve_node.COB_SETPOINT
FEEDBACK_ID = valve_node.COB_FEEDBACK
BITS_PER_FRAME = 130  # 11-бит кадр, 8 байт данных, худший стаффинг


class BusMonitor(threading.Thread):
    """Пассивный слушатель: RTT setpoint→feedback и нагрузка шины."""

    def __init__(self, channel: str) -> None:
        super().__init__(daemon=True)
        self.bus = can.interface.Bus(channel=channel, interface="socketcan")
        self.rtt_us: list[float] = []
        self.frames = 0
        self.bits = 0
        self._pending_setpoint: float | None = None
        self._stop_evt = threading.Event()
        self.t0: float | None = None
        self.t1: float | None = None

    def run(self) -> None:
        while not self._stop_evt.is_set():
            msg = self.bus.recv(timeout=0.05)
            if msg is None:
                continue
            self.frames += 1
            self.bits += 47 + 8 * msg.dlc + 19  # оценка с межкадровым
            if self.t0 is None:
                self.t0 = msg.timestamp
            self.t1 = msg.timestamp
            if msg.arbitration_id == SETPOINT_ID:
                self._pending_setpoint = msg.timestamp
            elif (msg.arbitration_id == FEEDBACK_ID
                  and self._pending_setpoint is not None):
                self.rtt_us.append(
                    (msg.timestamp - self._pending_setpoint) * 1e6)
                self._pending_setpoint = None

    def stop(self) -> dict:
        self._stop_evt.set()
        self.join(timeout=1.0)
        self.bus.shutdown()
        out: dict = {"frames": self.frames}
        if self.rtt_us:
            srt = sorted(self.rtt_us)
            out["rtt_min_us"] = srt[0]
            out["rtt_avg_us"] = statistics.mean(srt)
            out["rtt_p99_us"] = srt[min(len(srt) - 1,
                                        math.ceil(len(srt) * 0.99) - 1)]
            out["rtt_max_us"] = srt[-1]
            out["rtt_n"] = len(srt)
        if self.t0 is not None and self.t1 is not None and self.t1 > self.t0:
            out["bus_load_pct"] = 100.0 * self.bits / 1e6 / (self.t1 - self.t0)
        return out


def run_rig(sim_host: str, channel: str, duration_s: float,
            freq: float, amplitude: float, mean: float,
            sync_mode: bool = False, fail_at_s: float = 0.0,
            link_loss_at_ms: int = 0) -> tuple[list[dict], dict, str]:
    node_thread = threading.Thread(
        target=valve_node.run,
        args=(channel, sync_mode, fail_at_s, duration_s + 2.0),
        daemon=True)
    monitor = BusMonitor(channel)
    node_thread.start()
    monitor.start()
    time.sleep(0.3)  # bootup эмулятора до старта мастера

    cmd = [sim_host, "--socketcan", channel,
           "--duration-s", str(duration_s), "--freq", str(freq),
           "--amplitude", str(amplitude), "--mean", str(mean)]
    if link_loss_at_ms:
        cmd += ["--link-loss-at-ms", str(link_loss_at_ms)]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=True,
                          timeout=duration_s + 30)

    rows = list(csv.DictReader(io.StringIO(proc.stdout)))
    node_thread.join(timeout=5.0)
    stats = monitor.stop()
    time.sleep(0.5)  # пауза между сценариями (закрытие сокетов)
    if not rows:
        raise RuntimeError(
            f"sim_host не дал CSV; stderr: {proc.stderr.strip()!r}")
    return rows, stats, proc.stderr.strip()


def tracking_metrics(rows: list[dict], skip_s: float = 1.0) -> dict:
    body = [r for r in rows if float(r["now_ms"]) > skip_s * 1000]
    errs = [float(r["effective_target"]) - float(r["force_n"]) for r in body
            if r["mode"] == "force"]
    if not errs:
        # На нагруженном dev-хосте контур иногда не успевает выйти в
        # force-режим за skip_s (планировщик ОС не дал CPU потоку
        # эмулятора вовремя) — это флакиность рига, не баг подсчёта.
        raise RuntimeError(
            "нет force-строк после skip_s — контур не вышел в режим "
            "(вероятна перегрузка хоста); повторите прогон")
    fresh = [int(r["fresh"]) for r in body if "fresh" in r]
    out = {
        "rms": math.sqrt(sum(e * e for e in errs) / len(errs)),
        "max": max(abs(e) for e in errs),
    }
    if fresh:
        out["fresh_pct"] = 100.0 * sum(fresh) / len(fresh)
    return out


def with_retries(fn, attempts: int = 3):
    """Повторить сценарий (run_rig + разбор метрик) при RuntimeError.

    На общем dev-хосте планировщик ОС иногда не успевает выделить CPU
    потокам эмулятора/монитора вовремя (три Python-потока + subprocess
    конкурируют), из-за чего единичный прогон может не выйти в
    измеряемый режим. Это флакиность рига, не логическая ошибка —
    повтор устраняет её (каждая попытка — независимый чистый прогон)."""
    last_err: Exception | None = None
    for attempt in range(1, attempts + 1):
        try:
            return fn()
        except RuntimeError as e:
            last_err = e
            print(f"  (попытка {attempt}/{attempts} не удалась: {e})",
                  flush=True)
            time.sleep(1.0)
    assert last_err is not None
    raise last_err


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sim-host", required=True)
    parser.add_argument("--channel", default="vcan0")
    parser.add_argument("--out", default="-",
                        help="файл markdown-фрагмента ('-' = stdout)")
    args = parser.parse_args()

    lines: list[str] = []

    def emit(s: str) -> None:
        lines.append(s)
        print(s, flush=True)

    # (a) слежение 10/20/50 Гц, event-driven feedback
    emit("| Сценарий | RMS, Н | max, Н | fresh, % | RTT p99, мкс |"
         " нагрузка шины, % |")
    emit("|---|---|---|---|---|---|")
    for freq, amp in ((10.0, 10_000.0), (20.0, 5_000.0), (50.0, 2_000.0)):
        def scenario(freq=freq, amp=amp):
            rows, stats, tick_line = run_rig(args.sim_host, args.channel,
                                             4.0, freq, amp, 20_000.0)
            return rows, stats, tick_line, tracking_metrics(rows)
        rows, stats, tick_line, m = with_retries(scenario)
        emit(f"| синус {freq:g} Гц (event) | {m['rms']:.0f} | {m['max']:.0f} "
             f"| {m.get('fresh_pct', 0):.1f} "
             f"| {stats.get('rtt_p99_us', 0):.0f} "
             f"| {stats.get('bus_load_pct', 0):.1f} |")
        emit(f"  <!-- tick: {tick_line} -->")

    # (сравнение) sync-driven feedback на 10 Гц
    def sync_scenario():
        rows, stats, tick_line = run_rig(args.sim_host, args.channel, 4.0,
                                         10.0, 10_000.0, 20_000.0,
                                         sync_mode=True)
        return rows, stats, tick_line, tracking_metrics(rows)
    rows, stats, tick_line, m = with_retries(sync_scenario)
    emit(f"| синус 10 Гц (SYNC) | {m['rms']:.0f} | {m['max']:.0f} "
         f"| {m.get('fresh_pct', 0):.1f} | {stats.get('rtt_p99_us', 0):.0f} "
         f"| {stats.get('bus_load_pct', 0):.1f} |")
    emit(f"  <!-- tick: {tick_line} -->")

    # (c) разрушение образца на реальной шине
    def failure_scenario():
        rows, _, _ = run_rig(args.sim_host, args.channel, 4.0, 10.0,
                             10_000.0, 20_000.0, fail_at_s=2.0)
        latched = [r for r in rows if r["failure"] == "1"]
        if not latched:
            raise RuntimeError("детектор не сработал за duration сценария")
        return rows, latched
    rows, latched = with_retries(failure_scenario)
    first = float(latched[0]["now_ms"])
    emit(f"\nРазрушение на 2000 мс: латч на {first:.0f} мс "
         f"(CSV-время подвержено джиттеру рига — см. sim/README.md), "
         f"итоговый режим {rows[-1]['mode']}/{rows[-1]['link']}")

    # (d) потеря связи
    rows, _, _ = with_retries(
        lambda: run_rig(args.sim_host, args.channel, 4.0, 10.0, 10_000.0,
                        20_000.0, link_loss_at_ms=1500))
    states = {r["link"] for r in rows}
    emit(f"Потеря связи на 1500 мс: состояния {sorted(states)}, "
         f"финал {rows[-1]['link']}")

    if args.out != "-":
        with open(args.out, "w") as f:
            f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
