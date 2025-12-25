from __future__ import annotations

from pathlib import Path
from typing import Any, Literal
from uuid import UUID

import yaml
from pydantic import BaseModel, Field, HttpUrl, SecretStr


class ExperimentServiceConfig(BaseModel):
    base_url: HttpUrl = Field(default="http://localhost:8002")
    sensor_token: SecretStr
    timeout_s: float = Field(default=10.0, ge=0.1)


class TargetConfig(BaseModel):
    sensor_id: UUID
    run_id: UUID | None = None
    capture_session_id: UUID | None = None
    meta: dict[str, Any] = Field(default_factory=dict)


class BatchConfig(BaseModel):
    max_readings: int = Field(default=200, ge=1, le=10_000)
    flush_interval_ms: int = Field(default=500, ge=10, le=60_000)


class SyntheticSignalConfig(BaseModel):
    signal: str
    kind: Literal["sine", "saw", "square", "noise", "constant"]
    amplitude: float = 1.0
    offset: float = 0.0
    freq_hz: float = 1.0
    phase_rad: float = 0.0
    duty: float = Field(default=0.5, ge=0.0, le=1.0)  # for square
    noise_std: float = 0.1  # for noise


class SourceSyntheticConfig(BaseModel):
    type: Literal["synthetic"] = "synthetic"
    sample_hz: float = Field(default=50.0, gt=0.0, le=1000.0)
    signals: list[SyntheticSignalConfig]


class SourceUdpJsonConfig(BaseModel):
    type: Literal["udp_json"] = "udp_json"
    host: str = "0.0.0.0"
    port: int = Field(default=9999, ge=1, le=65535)


class SourceEsp32WsConfig(BaseModel):
    type: Literal["esp32_ws"] = "esp32_ws"
    url: str  # ws://...
    reconnect_delay_ms: int = Field(default=1000, ge=100, le=60_000)


SourceConfig = SourceSyntheticConfig | SourceUdpJsonConfig | SourceEsp32WsConfig


class AppConfig(BaseModel):
    experiment_service: ExperimentServiceConfig
    target: TargetConfig
    batch: BatchConfig = Field(default_factory=BatchConfig)
    source: SourceConfig


def load_config(path: str | Path) -> AppConfig:
    p = Path(path)
    data = yaml.safe_load(p.read_text(encoding="utf-8"))
    return AppConfig.model_validate(data)


