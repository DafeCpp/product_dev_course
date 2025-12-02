"""Application settings."""
from __future__ import annotations

from functools import lru_cache
from typing import Literal, cast

from pydantic import AnyHttpUrl, Field, PostgresDsn
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Core configuration for the Experiment Service."""

    model_config = SettingsConfigDict(env_file=(".env", "env.example"), env_file_encoding="utf-8")

    env: Literal["development", "staging", "production"] = "development"
    app_name: str = "experiment-service"
    host: str = "0.0.0.0"
    port: int = 8002

    database_url: PostgresDsn = Field(
        default=cast(PostgresDsn, "postgresql://postgres:postgres@localhost:5432/experiment_db")
    )
    db_pool_size: int = 20

    auth_service_url: AnyHttpUrl = Field(
        default=cast(AnyHttpUrl, "http://localhost:8001/api/v1")
    )
    rabbitmq_url: str = "amqp://guest:guest@localhost:5672/"
    telemetry_broker_url: str = "redis://localhost:6379/0"

    otel_exporter_endpoint: AnyHttpUrl | None = None


@lru_cache
def get_settings() -> Settings:
    """Cached settings instance."""
    return Settings()


settings = get_settings()

