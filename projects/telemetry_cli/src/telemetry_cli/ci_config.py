from __future__ import annotations

import os
import tomllib
from pathlib import Path
from typing import Any


_CONFIG_PATH = Path.home() / ".etp" / "config.toml"


def _load_toml() -> dict[str, Any]:
    if _CONFIG_PATH.exists():
        with _CONFIG_PATH.open("rb") as f:
            return tomllib.load(f)
    return {}


def get_api_url() -> str:
    value = os.environ.get("ETP_API_URL")
    if value:
        return value.rstrip("/")
    cfg = _load_toml()
    return str(cfg.get("api_url", "http://localhost:8002")).rstrip("/")


def get_token() -> str | None:
    value = os.environ.get("ETP_TOKEN")
    if value:
        return value
    cfg = _load_toml()
    return cfg.get("token")
