"""Tests for shared request metadata helpers."""
from __future__ import annotations

from unittest.mock import MagicMock

from backend_common.api import extract_client_ip


def test_extract_client_ip_uses_first_forwarded_address() -> None:
    request = MagicMock()
    request.headers = {"X-Forwarded-For": "203.0.113.1, 10.0.0.1"}
    request.remote = "127.0.0.1"

    assert extract_client_ip(request) == "203.0.113.1"


def test_extract_client_ip_falls_back_to_request_remote() -> None:
    request = MagicMock()
    request.headers = {}
    request.remote = "127.0.0.1"

    assert extract_client_ip(request) == "127.0.0.1"


def test_extract_client_ip_returns_none_when_request_has_no_address() -> None:
    request = MagicMock()
    request.headers = {}
    request.remote = None

    assert extract_client_ip(request) is None
