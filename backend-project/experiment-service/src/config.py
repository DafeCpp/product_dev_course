"""Конфигурация сервиса."""
import os
from typing import Optional


class Settings:
    """Настройки приложения."""

    # Server
    HOST: str = os.getenv("HOST", "0.0.0.0")
    PORT: int = int(os.getenv("PORT", "8002"))

    # Database
    DATABASE_URL: str = os.getenv(
        "DATABASE_URL",
        "postgresql://postgres:postgres@localhost:5432/experiment_db"
    )
    DB_POOL_MIN_SIZE: int = int(os.getenv("DB_POOL_MIN_SIZE", "5"))
    DB_POOL_MAX_SIZE: int = int(os.getenv("DB_POOL_MAX_SIZE", "20"))

    # Auth
    JWT_SECRET: str = os.getenv("JWT_SECRET", "your-secret-key-change-in-production")
    AUTH_SERVICE_URL: str = os.getenv("AUTH_SERVICE_URL", "http://localhost:8001")

    # RabbitMQ
    RABBITMQ_URL: str = os.getenv(
        "RABBITMQ_URL",
        "amqp://guest:guest@localhost:5672/"
    )
    RABBITMQ_EXCHANGE: str = os.getenv("RABBITMQ_EXCHANGE", "events")

    # Logging
    LOG_LEVEL: str = os.getenv("LOG_LEVEL", "INFO")

    # Development
    DEBUG: bool = os.getenv("DEBUG", "false").lower() == "true"
    ENV: str = os.getenv("ENV", "development")


settings = Settings()

