#!/usr/bin/env python3
"""
RC Vehicle UDP Telemetry Receiver & Controller.

Receives binary telemetry frames from ESP32 over UDP and writes CSV.
Also sends control commands (START/STOP/STATUS/PING) to ESP32.

Usage:
    # Full cycle: start streaming, record to CSV, stop on Ctrl+C
    python3 udp_telem.py record --esp 192.168.4.1 --csv output.csv

    # Just listen (streaming already started via WebSocket or other means)
    python3 udp_telem.py listen --csv output.csv

    # Send commands only
    python3 udp_telem.py start --esp 192.168.4.1 --port 5555 --hz 100
    python3 udp_telem.py stop --esp 192.168.4.1
    python3 udp_telem.py status --esp 192.168.4.1
    python3 udp_telem.py ping --esp 192.168.4.1

No external dependencies — uses only Python standard library.
"""

from __future__ import annotations

import argparse
import csv
import json
import signal
import socket
import struct
import sys
import time
from pathlib import Path

# ---------------------------------------------------------------------------
# Protocol constants
# ---------------------------------------------------------------------------

MAGIC = b"\x52\x54"  # "RT"
PACKET_VERSION = 1
HEADER_SIZE = 7  # 2 magic + 1 version + 4 seq
FRAME_SIZE = 80  # sizeof(TelemetryLogFrame)
PACKET_SIZE = HEADER_SIZE + FRAME_SIZE  # 87 bytes

CONTROL_PORT = 5556
DEFAULT_DATA_PORT = 5555

# struct format for TelemetryLogFrame (little-endian):
# uint32_t ts_ms + 19 x float
FRAME_FMT = "<I19f"
assert struct.calcsize(FRAME_FMT) == FRAME_SIZE

HEADER_FMT = "<2sBIx"  # We'll parse header manually for clarity

FIELD_NAMES = [
    "ts_ms",
    "ax", "ay", "az",
    "gx", "gy", "gz",
    "vx", "vy",
    "slip_deg", "speed_ms",
    "throttle", "steering",
    "pitch_deg", "roll_deg", "yaw_deg",
    "yaw_rate_dps",
    "oversteer_active",
    "rc_throttle", "rc_steering",
]


# ---------------------------------------------------------------------------
# Decode
# ---------------------------------------------------------------------------

def decode_packet(data: bytes) -> tuple[int, tuple] | None:
    """Decode a UDP telemetry packet. Returns (seq, field_values) or None."""
    if len(data) < PACKET_SIZE:
        return None
    if data[0:2] != MAGIC:
        return None
    version = data[2]
    if version != PACKET_VERSION:
        return None
    seq = struct.unpack_from("<I", data, 3)[0]
    values = struct.unpack_from(FRAME_FMT, data, HEADER_SIZE)
    return seq, values


# ---------------------------------------------------------------------------
# Control commands
# ---------------------------------------------------------------------------

def send_command(esp_ip: str, command: str, timeout: float = 2.0) -> dict | None:
    """Send a text command to ESP32 control port and return JSON response."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    try:
        sock.sendto(command.encode(), (esp_ip, CONTROL_PORT))
        data, _ = sock.recvfrom(512)
        return json.loads(data.decode())
    except socket.timeout:
        print(f"Timeout waiting for response from {esp_ip}:{CONTROL_PORT}", file=sys.stderr)
        return None
    finally:
        sock.close()


def cmd_start(args: argparse.Namespace) -> int:
    cmd = f"START {args.port} {args.hz}"
    resp = send_command(args.esp, cmd)
    if resp is None:
        return 1
    print(json.dumps(resp, indent=2))
    return 0 if resp.get("ok") else 1


def cmd_stop(args: argparse.Namespace) -> int:
    resp = send_command(args.esp, "STOP")
    if resp is None:
        return 1
    print(json.dumps(resp, indent=2))
    return 0 if resp.get("ok") else 1


def cmd_status(args: argparse.Namespace) -> int:
    resp = send_command(args.esp, "STATUS")
    if resp is None:
        return 1
    print(json.dumps(resp, indent=2))
    return 0


def cmd_ping(args: argparse.Namespace) -> int:
    t0 = time.monotonic()
    resp = send_command(args.esp, "PING")
    rtt_ms = (time.monotonic() - t0) * 1000
    if resp is None:
        return 1
    print(f"Pong: uptime={resp.get('uptime_ms')} ms, RTT={rtt_ms:.1f} ms")
    return 0


# ---------------------------------------------------------------------------
# Listen / Record
# ---------------------------------------------------------------------------

class Receiver:
    def __init__(self, port: int, csv_path: str | None, quiet: bool = False):
        self.port = port
        self.csv_path = csv_path
        self.quiet = quiet
        self.count = 0
        self.dropped = 0
        self.last_seq: int | None = None
        self.start_time = 0.0
        self._running = True
        self._csv_file = None
        self._writer = None

    def stop(self):
        self._running = False

    def run(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("0.0.0.0", self.port))
        sock.settimeout(1.0)  # allow periodic check of _running

        if self.csv_path:
            self._csv_file = open(self.csv_path, "w", newline="")
            self._writer = csv.writer(self._csv_file)
            self._writer.writerow(FIELD_NAMES)

        self.start_time = time.monotonic()
        print(f"Listening on UDP :{self.port}" +
              (f", writing to {self.csv_path}" if self.csv_path else ""))

        try:
            while self._running:
                try:
                    data, addr = sock.recvfrom(256)
                except socket.timeout:
                    continue

                result = decode_packet(data)
                if result is None:
                    continue

                seq, values = result

                # Loss detection
                if self.last_seq is not None:
                    if seq == 0 and self.last_seq > 0:
                        # ESP32 restarted
                        if not self.quiet:
                            print(f"\n[RESET] seq reset (ESP32 restarted?)", file=sys.stderr)
                    elif seq > self.last_seq + 1:
                        gap = seq - self.last_seq - 1
                        self.dropped += gap
                        if not self.quiet:
                            print(f"\n[LOSS] expected seq={self.last_seq + 1}, "
                                  f"got {seq} (lost {gap})", file=sys.stderr)
                self.last_seq = seq

                # Write CSV
                if self._writer:
                    self._writer.writerow(values)

                self.count += 1

                # Periodic flush and status
                if self.count % 500 == 0:
                    if self._csv_file:
                        self._csv_file.flush()
                    if not self.quiet:
                        elapsed = time.monotonic() - self.start_time
                        rate = self.count / elapsed if elapsed > 0 else 0
                        print(f"\r  {self.count} frames, {self.dropped} lost, "
                              f"{rate:.0f} Hz", end="", flush=True)

        finally:
            sock.close()
            if self._csv_file:
                self._csv_file.close()

        self._print_summary()

    def _print_summary(self):
        elapsed = time.monotonic() - self.start_time
        total = self.count + self.dropped
        loss_pct = (self.dropped / total * 100) if total > 0 else 0
        rate = self.count / elapsed if elapsed > 0 else 0
        print(f"\n--- Summary ---")
        print(f"  Received:  {self.count}")
        print(f"  Lost:      {self.dropped} ({loss_pct:.1f}%)")
        print(f"  Duration:  {elapsed:.1f}s")
        print(f"  Avg rate:  {rate:.1f} Hz")
        if self.csv_path:
            print(f"  CSV:       {self.csv_path}")


def cmd_listen(args: argparse.Namespace) -> int:
    receiver = Receiver(args.port, args.csv, args.quiet)
    signal.signal(signal.SIGINT, lambda *_: receiver.stop())
    receiver.run()
    return 0


def cmd_record(args: argparse.Namespace) -> int:
    """Full cycle: start streaming, listen, stop on Ctrl+C."""
    # Start streaming
    cmd = f"START {args.port} {args.hz}"
    print(f"Sending START to {args.esp}...")
    resp = send_command(args.esp, cmd)
    if resp is None:
        print("Failed to contact ESP32", file=sys.stderr)
        return 1
    if not resp.get("ok"):
        print(f"START failed: {resp}", file=sys.stderr)
        return 1
    print(f"Streaming started: {resp.get('ip')}:{resp.get('port')} @ {resp.get('hz')} Hz")

    # Listen
    receiver = Receiver(args.port, args.csv, args.quiet)

    def on_signal(*_):
        receiver.stop()

    signal.signal(signal.SIGINT, on_signal)
    receiver.run()

    # Stop streaming
    print(f"Sending STOP to {args.esp}...")
    stop_resp = send_command(args.esp, "STOP")
    if stop_resp:
        print(f"Stopped: {json.dumps(stop_resp)}")

    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="RC Vehicle UDP Telemetry Receiver & Controller",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    # record
    p = sub.add_parser("record", help="Start streaming + record to CSV + stop on Ctrl+C")
    p.add_argument("--esp", required=True, help="ESP32 IP address")
    p.add_argument("--port", type=int, default=DEFAULT_DATA_PORT, help="UDP data port (default: 5555)")
    p.add_argument("--hz", type=int, default=100, choices=[10, 20, 50, 100], help="Streaming frequency")
    p.add_argument("--csv", default="telemetry.csv", help="Output CSV file (default: telemetry.csv)")
    p.add_argument("--quiet", "-q", action="store_true", help="Suppress progress output")
    p.set_defaults(func=cmd_record)

    # listen
    p = sub.add_parser("listen", help="Listen for telemetry (streaming must already be active)")
    p.add_argument("--port", type=int, default=DEFAULT_DATA_PORT, help="UDP data port (default: 5555)")
    p.add_argument("--csv", default=None, help="Output CSV file (optional)")
    p.add_argument("--quiet", "-q", action="store_true", help="Suppress progress output")
    p.set_defaults(func=cmd_listen)

    # start
    p = sub.add_parser("start", help="Send START command to ESP32")
    p.add_argument("--esp", required=True, help="ESP32 IP address")
    p.add_argument("--port", type=int, default=DEFAULT_DATA_PORT, help="UDP data port")
    p.add_argument("--hz", type=int, default=100, choices=[10, 20, 50, 100])
    p.set_defaults(func=cmd_start)

    # stop
    p = sub.add_parser("stop", help="Send STOP command to ESP32")
    p.add_argument("--esp", required=True, help="ESP32 IP address")
    p.set_defaults(func=cmd_stop)

    # status
    p = sub.add_parser("status", help="Query streaming status")
    p.add_argument("--esp", required=True, help="ESP32 IP address")
    p.set_defaults(func=cmd_status)

    # ping
    p = sub.add_parser("ping", help="Ping ESP32")
    p.add_argument("--esp", required=True, help="ESP32 IP address")
    p.set_defaults(func=cmd_ping)

    args = parser.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
