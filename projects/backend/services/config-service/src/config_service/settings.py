"""Application settings."""
from __future__ import annotations

from functools import lru_cache
from typing import cast

from pydantic import AnyHttpUrl, Field, PostgresDsn

from backend_common.settings.base import BaseServiceSettings


class Settings(BaseServiceSettings):
    """Core configuration for the Config Service."""

    app_name: str = "config-service"
    port: int = 8005

    database_url: PostgresDsn = Field(
        default=cast(PostgresDsn, "postgresql://config_user:config_password@localhost:5433/config_db")
    )

    auth_service_url: AnyHttpUrl = Field(
        default=cast(AnyHttpUrl, "http://localhost:8001/api/v1")
    )

    otel_exporter_endpoint: AnyHttpUrl | None = None

    # Idempotency key TTL
    idempotency_ttl_minutes: int = 15

    # Background worker cleanup interval
    worker_interval_seconds: float = 60.0


@lru_cache
def get_settings() -> Settings:
    """Cached settings instance."""
    return Settings()


settings = get_settings()
