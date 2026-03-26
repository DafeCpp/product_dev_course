"""Application settings."""
from __future__ import annotations

from functools import lru_cache
from typing import cast

from pydantic import AnyHttpUrl, Field, PostgresDsn

from backend_common.settings.base import BaseServiceSettings


class Settings(BaseServiceSettings):
    """Core configuration for the Script Service."""

    app_name: str = "script-service"
    port: int = 8004

    database_url: PostgresDsn = Field(
        default=cast(PostgresDsn, "postgresql://postgres:postgres@localhost:5432/script_db")
    )

    rabbitmq_url: str = "amqp://guest:guest@rabbitmq:5672/"
    auth_service_url: AnyHttpUrl = Field(
        default=cast(AnyHttpUrl, "http://auth-service:8001")
    )


@lru_cache
def get_settings() -> Settings:
    """Cached settings instance."""
    return Settings()


settings = get_settings()
