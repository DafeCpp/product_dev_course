"""Tests for the AUTH_SERVICE_URL contract helper."""
import pytest

from backend_common.auth_service import auth_service_root


@pytest.mark.parametrize(
    "url",
    [
        "http://auth-service:8001",
        "http://auth-service:8001/",
        "http://auth-service:8001/api/v1",
        "http://auth-service:8001/api/v1/",
    ],
)
def test_auth_service_root_accepts_root_and_legacy_api_url(url: str) -> None:
    assert auth_service_root(url) == "http://auth-service:8001"


def test_auth_service_root_keeps_a_path_prefix() -> None:
    # auth-service mounted behind a path prefix keeps that prefix.
    assert auth_service_root("http://gw/auth/api/v1") == "http://gw/auth"
