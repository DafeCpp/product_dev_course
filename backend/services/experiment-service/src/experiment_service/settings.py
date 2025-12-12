"""Application settings."""
from __future__ import annotations

from functools import lru_cache
from typing import Literal, cast

from pydantic import AnyHttpUrl, Field, PostgresDsn, model_validator
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

    # Use a string field to avoid JSON parsing by pydantic-settings
    cors_allowed_origins_str: str = Field(
        default="http://localhost:3000,http://localhost:8080",
        alias="CORS_ALLOWED_ORIGINS",
    )

    # This field is populated by the validator, not from env vars
    cors_allowed_origins: list[str] = Field(
        default=["http://localhost:3000", "http://localhost:8080"],
        validation_alias="__cors_allowed_origins_internal__",  # Use a non-existent alias to prevent env parsing
    )

    @model_validator(mode="after")
    def parse_cors_origins(self) -> "Settings":
        """Parse CORS origins from comma-separated string after model initialization."""
        value = self.cors_allowed_origins_str
        if value:
            self.cors_allowed_origins = [
                origin.strip() for origin in value.split(",") if origin.strip()
            ]
        return self


@lru_cache
def get_settings() -> Settings:
    """Cached settings instance."""
    return Settings()


settings = get_settings()

