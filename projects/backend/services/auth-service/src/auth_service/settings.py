"""Application settings."""
from __future__ import annotations

from functools import lru_cache
from typing import Literal, cast

from pydantic import AnyHttpUrl, Field, PostgresDsn, model_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Core configuration for the Auth Service."""

    model_config = SettingsConfigDict(env_file=(".env", "env.example"), env_file_encoding="utf-8")

    env: Literal["development", "staging", "production"] = "development"
    app_name: str = "auth-service"
    host: str = "0.0.0.0"
    port: int = 8001

    database_url: PostgresDsn = Field(
        default=cast(PostgresDsn, "postgresql://postgres:postgres@localhost:5432/auth_db")
    )
    db_pool_size: int = 20

    jwt_secret: str = Field(default="dev-secret-key-change-in-production")
    jwt_algorithm: str = "HS256"
    access_token_ttl_sec: int = 900  # 15 minutes
    refresh_token_ttl_sec: int = 1209600  # 14 days

    bcrypt_rounds: int = 12

    # Use a string field to avoid JSON parsing by pydantic-settings
    cors_allowed_origins_str: str = Field(
        default="http://localhost:3000,http://localhost:8080",
        alias="CORS_ALLOWED_ORIGINS",
    )

    # This field is populated by the validator, not from env vars
    cors_allowed_origins: list[str] = Field(
        default=["http://localhost:3000", "http://localhost:8080"],
        validation_alias="__cors_allowed_origins_internal__",
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

    @property
    def cors_allowed_origins_list(self) -> list[str]:
        """Get CORS allowed origins list."""
        return self.cors_allowed_origins


@lru_cache
def get_settings() -> Settings:
    """Cached settings instance."""
    return Settings()


settings = get_settings()

