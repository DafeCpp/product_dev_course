"""Base settings class with common fields."""
from __future__ import annotations

from typing import Literal, cast

from pydantic import AnyHttpUrl, Field, PostgresDsn, model_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class BaseServiceSettings(BaseSettings):
    """Base settings class with common fields for all services."""

    model_config = SettingsConfigDict(env_file=(".env", "env.example"), env_file_encoding="utf-8")

    env: Literal["development", "staging", "production"] = "development"
    app_name: str
    host: str = "0.0.0.0"
    port: int

    database_url: PostgresDsn = Field(
        default=cast(PostgresDsn, "postgresql://postgres:postgres@localhost:5432/db")
    )
    db_pool_size: int = 20

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
    def parse_cors_origins(self) -> "BaseServiceSettings":
        """Parse CORS origins from comma-separated string after model initialization."""
        value = self.cors_allowed_origins_str
        if value:
            self.cors_allowed_origins = [
                origin.strip() for origin in value.split(",") if origin.strip()
            ]
        return self

