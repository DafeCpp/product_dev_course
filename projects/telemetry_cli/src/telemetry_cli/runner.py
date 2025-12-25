from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator

from telemetry_cli.config import AppConfig
from telemetry_cli.models import TelemetryReading
from telemetry_cli.sink_experiment_service import ExperimentServiceClient
from telemetry_cli.sources import esp32_ws_source, synthetic_source, udp_json_source


async def _select_source(cfg: AppConfig) -> AsyncIterator[TelemetryReading]:
    if cfg.source.type == "synthetic":
        return synthetic_source(cfg.source)
    if cfg.source.type == "udp_json":
        return udp_json_source(cfg.source)
    if cfg.source.type == "esp32_ws":
        return esp32_ws_source(cfg.source)
    raise ValueError(f"Unknown source type: {cfg.source}")


async def run_agent(cfg: AppConfig) -> None:
    flush_interval_s = cfg.batch.flush_interval_ms / 1000.0
    max_readings = cfg.batch.max_readings

    async with ExperimentServiceClient(
        base_url=str(cfg.experiment_service.base_url),
        sensor_token=cfg.experiment_service.sensor_token.get_secret_value(),
        timeout_s=cfg.experiment_service.timeout_s,
    ) as client:
        readings: list[TelemetryReading] = []
        last_flush = asyncio.get_running_loop().time()

        source_iter = await _select_source(cfg)
        async for r in source_iter:
            readings.append(r)
            now = asyncio.get_running_loop().time()
            should_flush = len(readings) >= max_readings or (now - last_flush) >= flush_interval_s
            if not should_flush:
                continue

            batch = readings
            readings = []
            last_flush = now
            try:
                await client.ingest(
                    sensor_id=cfg.target.sensor_id,
                    run_id=cfg.target.run_id,
                    capture_session_id=cfg.target.capture_session_id,
                    meta=cfg.target.meta,
                    readings=batch,
                )
            except Exception:
                # Best-effort delivery: drop batch to keep real-time behaviour.
                continue


