"""Application settings."""
from __future__ import annotations

from functools import lru_cache
from typing import cast

from pydantic import Field, PostgresDsn

from backend_common.settings.base import BaseServiceSettings


class Settings(BaseServiceSettings):
    """Core configuration for Telemetry Ingest Service."""

    app_name: str = "telemetry-ingest-service"
    port: int = 8003

    # MVP: shared DB with experiment-service
    database_url: PostgresDsn = Field(
        default=cast(PostgresDsn, "postgresql://postgres:postgres@localhost:5432/experiment_db")
    )


@lru_cache
def get_settings() -> Settings:
    return Settings()


settings = get_settings()

