"""Shared, mutable QoS configuration for experiment-service.

All fields read from this singleton on request handling, enabling runtime
configuration updates via config-service (through the QoS poller):

- ``rate_limit_max_requests``: per-user requests per minute on the heavy
  endpoints, see ``middleware/api_rate_limit.py``.
- ``downstream_timeout_seconds``: total timeout of each audit write to
  auth-service, see ``services/audit_client.py``.

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
            downstream_timeout_seconds=5.0,
        )


QOS_CONFIG = ExperimentQosConfig.from_settings(_settings)
