"""Base settings class with common fields."""
from __future__ import annotations

from pathlib import Path
from typing import Any, Literal, cast

from pydantic import AnyHttpUrl, Field, PostgresDsn, model_validator
from pydantic_settings import BaseSettings, SettingsConfigDict

from backend_common.settings.yaml_loader import load_service_yaml


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

    @model_validator(mode="before")
    @classmethod
    def load_from_yaml(cls, values: dict[str, Any] | Any) -> dict[str, Any]:
        """Load database settings from service.yaml if available."""
        if not isinstance(values, dict):
            values = {}

        yaml_config = load_service_yaml()
        if not yaml_config:
            return values

        # Extract app_name from service.yaml (use 'name' field if available)
        if "name" in yaml_config and "app_name" not in values:
            values["app_name"] = yaml_config["name"]

        # Extract database settings from service.yaml
        db_config = yaml_config.get("database", {})
        if db_config:
            # Update database_url if provided and not already set
            if "url" in db_config and "database_url" not in values:
                values["database_url"] = db_config["url"]
            # Update db_pool_size if provided and not already set
            if "pool_size" in db_config and "db_pool_size" not in values:
                values["db_pool_size"] = db_config["pool_size"]

        return values

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

