from __future__ import annotations

from unittest.mock import patch

from backend_common.settings.base import BaseServiceSettings


class ExampleSettings(BaseServiceSettings):
    app_name: str = "example"
    port: int = 8080


def test_settings_load_yaml_values_without_overriding_explicit_values() -> None:
    yaml_config = {
        "name": "yaml-service",
        "database": {
            "url": "postgresql://yaml:secret@db:5432/yaml_db",
            "pool_size": 7,
        },
    }

    with patch("backend_common.settings.base.load_service_yaml", return_value=yaml_config):
        from_yaml = ExampleSettings()
        explicit = ExampleSettings(app_name="explicit", database_url="postgresql://user:pass@db:5432/app", db_pool_size=3)

    assert from_yaml.app_name == "yaml-service"
    assert str(from_yaml.database_url) == "postgresql://yaml:secret@db:5432/yaml_db"
    assert from_yaml.db_pool_size == 7
    assert explicit.app_name == "explicit"
    assert str(explicit.database_url) == "postgresql://user:pass@db:5432/app"
    assert explicit.db_pool_size == 3


def test_settings_parse_cors_origins_and_ignore_non_mapping_validator_input() -> None:
    with patch("backend_common.settings.base.load_service_yaml", return_value={}):
        settings = ExampleSettings(CORS_ALLOWED_ORIGINS=" https://one.example, ,https://two.example ")

    assert settings.cors_allowed_origins == ["https://one.example", "https://two.example"]
    assert BaseServiceSettings.load_from_yaml("not-a-mapping") == {}
