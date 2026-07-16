"""Tests for auth-service URL normalization in the audit client."""
from unittest.mock import Mock

import pytest
from aiohttp import ClientSession

from experiment_service.services.audit_client import AuditClient


@pytest.mark.parametrize(
    ("auth_service_url", "expected_endpoint"),
    [
        ("http://auth-service:8001", "http://auth-service:8001/api/v1/internal/audit"),
        ("http://auth-service:8001/", "http://auth-service:8001/api/v1/internal/audit"),
        ("http://auth-service:8001/api/v1", "http://auth-service:8001/api/v1/internal/audit"),
        ("http://auth-service:8001/api/v1/", "http://auth-service:8001/api/v1/internal/audit"),
    ],
)
def test_audit_client_normalizes_auth_service_url(
    auth_service_url: str,
    expected_endpoint: str,
) -> None:
    client = AuditClient(auth_service_url, Mock(spec=ClientSession))

    assert client._endpoint == expected_endpoint
