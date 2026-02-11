"""Application settings."""
from __future__ import annotations

import warnings
from functools import lru_cache
from typing import cast

from pydantic import Field, PostgresDsn, model_validator

from backend_common.settings.base import BaseServiceSettings

_INSECURE_JWT_SECRETS = frozenset({
    "dev-secret-key-change-in-production",
    "secret",
    "changeme",
    "test",
})


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

    @model_validator(mode="after")
    def _warn_insecure_jwt_secret(self) -> "Settings":
        """Emit a loud warning when the JWT secret is a known insecure default."""
        if self.jwt_secret in _INSECURE_JWT_SECRETS:
            warnings.warn(
                "SECURITY WARNING: jwt_secret is set to a known insecure default "
                f"({self.jwt_secret!r}). Set JWT_SECRET env variable to a strong, "
                "random value before deploying to staging/production.",
                stacklevel=1,
            )
        elif len(self.jwt_secret) < 32:
            warnings.warn(
                "SECURITY WARNING: jwt_secret is shorter than 32 characters. "
                "Use a longer secret for production deployments.",
                stacklevel=1,
            )
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

