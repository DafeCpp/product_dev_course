from __future__ import annotations

import asyncio
import json
from collections.abc import AsyncIterator
from datetime import datetime, timezone
from typing import Any

import aiohttp

from telemetry_cli.config import SourceEsp32WsConfig
from telemetry_cli.models import TelemetryReading, utc_now


def _ts_from_telem(msg: dict[str, Any]) -> datetime:
    if "ts_ms" in msg:
        return datetime.fromtimestamp(float(msg["ts_ms"]) / 1000.0, tz=timezone.utc)
    return utc_now()


def _flatten_esp32_telem(msg: dict[str, Any]) -> list[TelemetryReading]:
    """
    Maps the draft ESP32->browser telemetry JSON into signal readings.
    Expected message shape (see rc_vehicle docs/interfaces_protocols.md):
      {
        "type":"telem",
        "ts_ms": ...,
        "link": {"active_source":"rc", "rc_ok":true, "wifi_ok":true},
        "imu": {"ax":..., "ay":..., "az":..., "gx":..., "gy":..., "gz":...},
        "act": {"thr":..., "steer":...}
      }
    """
    ts = _ts_from_telem(msg)
    out: list[TelemetryReading] = []

    imu = msg.get("imu")
    if isinstance(imu, dict):
        for k in ("ax", "ay", "az", "gx", "gy", "gz"):
            if k in imu:
                try:
                    out.append(TelemetryReading(timestamp=ts, raw_value=float(imu[k]), signal=f"imu.{k}"))
                except Exception:
                    pass

    act = msg.get("act")
    if isinstance(act, dict):
        for k in ("thr", "steer"):
            if k in act:
                try:
                    out.append(TelemetryReading(timestamp=ts, raw_value=float(act[k]), signal=f"act.{k}"))
                except Exception:
                    pass

    link = msg.get("link")
    if isinstance(link, dict):
        if "active_source" in link:
            out.append(
                TelemetryReading(
                    timestamp=ts,
                    raw_value=1.0,
                    signal="link.active_source",
                    meta={"value": str(link["active_source"])},
                )
            )
        for k in ("rc_ok", "wifi_ok"):
            if k in link:
                v = link[k]
                out.append(TelemetryReading(timestamp=ts, raw_value=1.0 if bool(v) else 0.0, signal=f"link.{k}"))

    return out


async def esp32_ws_source(cfg: SourceEsp32WsConfig) -> AsyncIterator[TelemetryReading]:
    """
    WebSocket client source for ESP32 telemetry stream.
    Reconnects on any error.
    """
    while True:
        try:
            async with aiohttp.ClientSession() as session:
                async with session.ws_connect(cfg.url, heartbeat=10) as ws:
                    async for msg in ws:
                        if msg.type == aiohttp.WSMsgType.TEXT:
                            try:
                                payload = json.loads(msg.data)
                            except Exception:
                                continue
                            if not isinstance(payload, dict):
                                continue
                            if payload.get("type") != "telem":
                                continue
                            for r in _flatten_esp32_telem(payload):
                                yield r
                        elif msg.type in (aiohttp.WSMsgType.ERROR, aiohttp.WSMsgType.CLOSE):
                            break
        except Exception:
            pass
        await asyncio.sleep(cfg.reconnect_delay_ms / 1000.0)


