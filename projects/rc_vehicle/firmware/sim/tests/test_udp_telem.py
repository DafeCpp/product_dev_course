"""LOS-381: `tools/udp_telem.py` разбирает текущий кадр TelemetryLogFrame."""

import os
import re
import struct
import sys

TOOLS = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "tools"))
FIRMWARE = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, TOOLS)

import udp_telem as ut  # noqa: E402


def _read(*parts):
    with open(os.path.join(FIRMWARE, *parts), encoding="utf-8") as f:
        return f.read()


def test_frame_size_matches_firmware_static_assert():
    hdr = _read("common", "telemetry_log.hpp")
    m = re.search(r"static_assert\(sizeof\(TelemetryLogFrame\) == (\d+)", hdr)
    assert m and int(m.group(1)) == ut.FRAME_SIZE


def test_packet_version_matches_firmware():
    cfg = _read("common", "config.hpp")
    m = re.search(r"kPacketVersion = (\d+)", cfg)
    assert m and int(m.group(1)) == ut.PACKET_VERSION


def test_fields_match_web_parser():
    # Веб-выгрузка PSRAM и UDP-запись должны давать одинаковые колонки.
    js = _read("esp32_common", "web", "app.js")
    block = js[js.index("const FIELD_OFFSETS"):]
    block = block[:block.index("]);")]
    web = []
    for m in re.finditer(
            r"name: '(\w+)',\s*off: (\d+),\s*type: '(\w+)'\s*(?:,\s*bit: (\d+))?", block):
        bit = int(m.group(4)) if m.group(4) is not None else None
        web.append((m.group(1), int(m.group(2)), m.group(3), bit))
    assert web == ut.FIELDS


def test_decode_packet_roundtrip():
    frame = bytearray(ut.FRAME_SIZE)
    struct.pack_into("<I", frame, 0, 12345)
    struct.pack_into("<f", frame, 4, 0.25)        # ax
    struct.pack_into("<f", frame, 80, 0.4)        # cmd_throttle
    struct.pack_into("<f", frame, 120, -90.0)     # heading_rel_deg
    frame[127] = 3                                # drive_mode = Kids
    frame[129] = 0b0010                           # kids_accel_limit_active
    frame[131] = 0b0101                           # rc_ok + failsafe_active
    pkt = ut.MAGIC + bytes([ut.PACKET_VERSION]) + struct.pack("<I", 7) + bytes(frame)
    assert len(pkt) == ut.PACKET_SIZE

    seq, values = ut.decode_packet(pkt)
    row = dict(zip(ut.FIELD_NAMES, values))
    assert seq == 7
    assert row["ts_ms"] == 12345
    assert row["ax"] == 0.25
    assert abs(row["cmd_throttle"] - 0.4) < 1e-6
    assert row["heading_rel_deg"] == -90.0
    assert row["drive_mode"] == 3
    assert row["kids_accel_limit_active"] == 1
    assert row["kids_anti_spin_active"] == 0
    assert (row["rc_ok"], row["wifi_ok"], row["failsafe_active"]) == (1, 0, 1)


def test_decode_packet_rejects_old_80_byte_frame():
    pkt = ut.MAGIC + bytes([ut.PACKET_VERSION]) + struct.pack("<I", 1) + bytes(80)
    assert ut.decode_packet(pkt) is None
