"""create_app startup wiring."""
from __future__ import annotations

import pytest

from experiment_service import main
from experiment_service.settings import settings


@pytest.mark.parametrize("enabled", [True, False])
def test_s3_bucket_check_follows_setting(monkeypatch: pytest.MonkeyPatch, enabled: bool) -> None:
    monkeypatch.setattr(settings, "s3_ensure_bucket_on_startup", enabled)
    app = main.create_app()
    assert (main.init_s3_bucket in app.on_startup) is enabled
