from __future__ import annotations

import asyncio
import json
from collections.abc import AsyncIterator
from datetime import datetime, timezone
from typing import Any

from telemetry_cli.config import SourceUdpJsonConfig
from telemetry_cli.models import TelemetryReading, utc_now


def _parse_ts(payload: dict[str, Any]) -> datetime:
    # Supported:
    # - ts_ms: epoch ms
    # - timestamp: RFC3339
    if "ts_ms" in payload:
        return datetime.fromtimestamp(float(payload["ts_ms"]) / 1000.0, tz=timezone.utc)
    if "timestamp" in payload:
        # minimal RFC3339 parser; accept "Z"
        s = str(payload["timestamp"]).replace("Z", "+00:00")
        return datetime.fromisoformat(s)
    return utc_now()


class _UdpQueueProtocol(asyncio.DatagramProtocol):
    def __init__(self, queue: "asyncio.Queue[bytes]"):
        self._queue = queue

    def datagram_received(self, data: bytes, addr):  # noqa: ANN001
        self._queue.put_nowait(data)


async def udp_json_source(cfg: SourceUdpJsonConfig) -> AsyncIterator[TelemetryReading]:
    """
    UDP JSON source.

    Supported payloads:
    1) Single reading:
       {"ts_ms": 1734690000000, "signal": "imu.ax", "raw_value": 0.01, "meta": {...}}
    2) Multi-signal sample:
       {"ts_ms": 1734690000000, "values": {"imu.ax": 0.01, "imu.ay": 0.02}, "meta": {...}}
    """
    queue: asyncio.Queue[bytes] = asyncio.Queue(maxsize=10_000)
    loop = asyncio.get_running_loop()
    transport, _protocol = await loop.create_datagram_endpoint(
        lambda: _UdpQueueProtocol(queue), local_addr=(cfg.host, cfg.port)
    )
    try:
        while True:
            data = await queue.get()
            try:
                payload = json.loads(data.decode("utf-8"))
            except Exception:
                continue
            if not isinstance(payload, dict):
                continue

            ts = _parse_ts(payload)
            meta = payload.get("meta") if isinstance(payload.get("meta"), dict) else {}

            if "values" in payload and isinstance(payload["values"], dict):
                for signal, raw_value in payload["values"].items():
                    try:
                        yield TelemetryReading(
                            timestamp=ts,
                            raw_value=float(raw_value),
                            signal=str(signal),
                            meta=meta,
                        )
                    except Exception:
                        continue
                continue

            if "signal" in payload and "raw_value" in payload:
                try:
                    yield TelemetryReading(
                        timestamp=ts,
                        raw_value=float(payload["raw_value"]),
                        signal=str(payload["signal"]),
                        meta=meta,
                    )
                except Exception:
                    continue
    finally:
        transport.close()


