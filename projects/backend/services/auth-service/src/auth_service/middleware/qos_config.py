"""Shared, mutable QoS configuration for auth-service.

All fields read from this singleton on request handling, enabling runtime
configuration updates via config-service (through the QoS poller).

Thread safety: asyncio is single-threaded; field updates happen in a single
synchronous block without ``await``, so mutation is atomic by construction.
"""
from __future__ import annotations

from dataclasses import dataclass

from auth_service.settings import Settings, settings as _settings


@dataclass
class AuthQosConfig:
    """Mutable QoS settings for auth-service."""

    access_token_ttl_sec: int
    refresh_token_ttl_sec: int

    @classmethod
    def from_settings(cls, s: Settings) -> "AuthQosConfig":
        return cls(
            access_token_ttl_sec=s.access_token_ttl_sec,
            refresh_token_ttl_sec=s.refresh_token_ttl_sec,
        )


QOS_CONFIG = AuthQosConfig.from_settings(_settings)
