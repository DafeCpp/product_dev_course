"""Application settings."""
from __future__ import annotations

from functools import lru_cache
from typing import cast

from pydantic import Field, PostgresDsn

from backend_common.settings.base import BaseServiceSettings


class Settings(BaseServiceSettings):
    """Core configuration for the Auth Service."""

    app_name: str = "auth-service"
    port: int = 8001

    database_url: PostgresDsn = Field(
        default=cast(PostgresDsn, "postgresql://postgres:postgres@localhost:5432/auth_db")
    )

    jwt_secret: str = Field(default="dev-secret-key-change-in-production")
    jwt_algorithm: str = "HS256"
    access_token_ttl_sec: int = 900  # 15 minutes
    refresh_token_ttl_sec: int = 1209600  # 14 days

    bcrypt_rounds: int = 12

    @property
    def cors_allowed_origins_list(self) -> list[str]:
        """Get CORS allowed origins list."""
        return self.cors_allowed_origins


@lru_cache
def get_settings() -> Settings:
    """Cached settings instance."""
    return Settings()


settings = get_settings()

