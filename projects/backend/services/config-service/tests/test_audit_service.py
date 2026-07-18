"""Tests for structured config audit redaction."""

from unittest.mock import MagicMock

from config_service.services import audit_service
from config_service.services.audit_service import AuditService


def test_sensitive_audit_value_is_always_redacted(monkeypatch):
    log = MagicMock()
    monkeypatch.setattr(audit_service, "logger", log)
    secret = {"token": "raw-secret-value"}

    AuditService().log(
        action="patch",
        actor="user",
        service_name="svc",
        config_type="feature_flag",
        config_id="id",
        key="secret",
        change_reason="rotate",
        is_critical=False,
        is_sensitive=True,
        correlation_id=None,
        source_ip=None,
        user_agent=None,
        value=secret,
    )

    _, kwargs = log.info.call_args
    assert kwargs["value"] == "***"
    assert "raw-secret-value" not in repr(log.info.call_args)
