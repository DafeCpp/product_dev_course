"""YAML configuration loader for service settings."""
from __future__ import annotations

import inspect
import yaml
from pathlib import Path
from typing import Any


def find_service_yaml(start_path: Path | None = None) -> Path | None:
    """Find service.yaml file by searching up from start_path.

    Args:
        start_path: Starting path to search from. If None, uses current working directory.

    Returns:
        Path to service.yaml if found, None otherwise.
    """
    if start_path is None:
        start_path = Path.cwd()

    current = Path(start_path).resolve()
    # Search up to 5 levels
    for _ in range(5):
        yaml_path = current / "service.yaml"
        if yaml_path.exists():
            return yaml_path
        parent = current.parent
        if parent == current:  # Reached root
            break
        current = parent

    return None


def load_service_yaml(yaml_path: Path | None = None) -> dict[str, Any]:
    """Load service.yaml configuration.

    Tries multiple strategies to find service.yaml:
    1. If yaml_path is provided, use it directly
    2. Try to find relative to calling module (if called from settings.py)
    3. Try to find from current working directory
    4. Try common container paths (/app/service.yaml)

    Args:
        yaml_path: Path to service.yaml. If None, attempts to find it automatically.

    Returns:
        Dictionary with configuration from service.yaml, empty dict if not found.
    """
    if yaml_path is not None:
        if yaml_path.exists():
            try:
                with yaml_path.open() as f:
                    return yaml.safe_load(f) or {}
            except Exception:
                return {}
        return {}

    # Strategy 1: Try to find relative to calling module
    # This works when called from settings.py in a service
    try:
        frame = inspect.currentframe()
        if frame is not None:
            caller_frame = frame.f_back
            if caller_frame is not None:
                caller_file = caller_frame.f_globals.get("__file__")
                if caller_file:
                    caller_path = Path(caller_file).resolve()
                    # From src/<service>/settings.py, go up to service root
                    # In container: /app/src/<service>/settings.py -> /app/service.yaml
                    # Locally: .../services/<service>/src/... -> .../services/<service>/service.yaml
                    possible_paths = [
                        caller_path.parent.parent.parent / "service.yaml",  # /app/service.yaml in container
                        caller_path.parent.parent.parent.parent / "service.yaml",  # Local dev
                    ]
                    for path in possible_paths:
                        if path.exists():
                            with path.open() as f:
                                return yaml.safe_load(f) or {}
    except Exception:
        pass

    # Strategy 2: Try from current working directory
    yaml_path = find_service_yaml()
    if yaml_path is not None and yaml_path.exists():
        try:
            with yaml_path.open() as f:
                return yaml.safe_load(f) or {}
        except Exception:
            pass

    # Strategy 3: Try common container paths
    container_paths = [
        Path("/app/service.yaml"),
    ]
    for path in container_paths:
        if path.exists():
            try:
                with path.open() as f:
                    return yaml.safe_load(f) or {}
            except Exception:
                pass

    return {}

