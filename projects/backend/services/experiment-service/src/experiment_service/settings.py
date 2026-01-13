"""Application settings."""
from __future__ import annotations

from functools import lru_cache
from typing import cast

from pydantic import AnyHttpUrl, Field, PostgresDsn

from backend_common.settings.base import BaseServiceSettings


class Settings(BaseServiceSettings):
    """Core configuration for the Experiment Service."""

    app_name: str = "experiment-service"
    port: int = 8002

    database_url: PostgresDsn = Field(
        default=cast(PostgresDsn, "postgresql://postgres:postgres@localhost:5432/experiment_db")
    )

    auth_service_url: AnyHttpUrl = Field(
        default=cast(AnyHttpUrl, "http://localhost:8001/api/v1")
    )
    rabbitmq_url: str = "amqp://guest:guest@localhost:5672/"
    telemetry_broker_url: str = "redis://localhost:6379/0"

    otel_exporter_endpoint: AnyHttpUrl | None = None

    # Webhooks (MVP)
    webhook_dispatch_interval_seconds: float = 0.2
    webhook_request_timeout_seconds: float = 3.0
    webhook_max_attempts: int = 5
    webhook_dispatch_max_concurrency: int = 10
    webhook_target_max_concurrency: int = 1


@lru_cache
def get_settings() -> Settings:
    """Cached settings instance."""
    return Settings()


settings = get_settings()

