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
    Maps the ESP32->browser telemetry JSON into signal readings.
    Expected message shape (see rc_vehicle firmware/common/control_components.cpp):
      {
        "type": "telem",
        "ts_ms": ...,
        "link": {"rc_ok": true, "wifi_ok": true, "failsafe": false},
        "imu": {
          "ax":..., "ay":..., "az":..., "gx":..., "gy":..., "gz":...,
          "gyro_z_filtered":..., "forward_accel":...,
          "orientation": {"pitch":..., "roll":..., "yaw":...}
        },
        "ekf": {"vx":..., "vy":..., "yaw_rate":..., "slip_deg":..., "speed_ms":...},
        "act": {"throttle":..., "steering":...}
      }
    """
    ts = _ts_from_telem(msg)
    out: list[TelemetryReading] = []

    imu = msg.get("imu")
    if isinstance(imu, dict):
        for k in ("ax", "ay", "az", "gx", "gy", "gz", "gyro_z_filtered", "forward_accel"):
            if k in imu:
                try:
                    out.append(TelemetryReading(timestamp=ts, raw_value=float(imu[k]), signal=f"imu.{k}"))
                except Exception:
                    pass
        orientation = imu.get("orientation")
        if isinstance(orientation, dict):
            for k in ("pitch", "roll", "yaw"):
                if k in orientation:
                    try:
                        out.append(
                            TelemetryReading(timestamp=ts, raw_value=float(orientation[k]), signal=f"imu.orientation.{k}")
                        )
                    except Exception:
                        pass

    ekf = msg.get("ekf")
    if isinstance(ekf, dict):
        for k in ("vx", "vy", "yaw_rate", "slip_deg", "speed_ms"):
            if k in ekf:
                try:
                    out.append(TelemetryReading(timestamp=ts, raw_value=float(ekf[k]), signal=f"ekf.{k}"))
                except Exception:
                    pass

    act = msg.get("act")
    if isinstance(act, dict):
        for k in ("throttle", "steering"):
            if k in act:
                try:
                    out.append(TelemetryReading(timestamp=ts, raw_value=float(act[k]), signal=f"act.{k}"))
                except Exception:
                    pass

    link = msg.get("link")
    if isinstance(link, dict):
        for k in ("rc_ok", "wifi_ok", "failsafe"):
            if k in link:
                v = link[k]
                out.append(TelemetryReading(timestamp=ts, raw_value=1.0 if bool(v) else 0.0, signal=f"link.{k}"))

    warn = msg.get("warn")
    if isinstance(warn, dict):
        for k in ("oversteer",):
            if k in warn:
                v = warn[k]
                out.append(TelemetryReading(timestamp=ts, raw_value=1.0 if bool(v) else 0.0, signal=f"warn.{k}"))

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


