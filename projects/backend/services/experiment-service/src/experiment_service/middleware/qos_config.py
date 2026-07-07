"""Shared, mutable QoS configuration for experiment-service.

All fields read from this singleton on request handling, enabling runtime
configuration updates via config-service (through the QoS poller).

Thread safety: asyncio is single-threaded; field updates happen in a single
synchronous block without ``await``, so mutation is atomic by construction.
"""
from __future__ import annotations

from dataclasses import dataclass

from experiment_service.settings import Settings, settings as _settings


@dataclass
class ExperimentQosConfig:
    """Mutable QoS settings for experiment-service."""

    rate_limit_max_requests: int
    downstream_timeout_seconds: float

    @classmethod
    def from_settings(cls, s: Settings) -> "ExperimentQosConfig":
        return cls(
            rate_limit_max_requests=1000,
            downstream_timeout_seconds=30.0,
        )


QOS_CONFIG = ExperimentQosConfig.from_settings(_settings)
