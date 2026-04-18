"""Unit tests for backend_common.middleware.trace module."""
from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from aiohttp import web

from backend_common.middleware.trace import (
    REQUEST_ID_HEADER,
    TRACE_ID_HEADER,
    create_trace_middleware,
    get_safe_headers,
    is_valid_uuid,
)


class TestIsValidUuid:
    """Tests for is_valid_uuid function."""

    def test_valid_uuid4(self):
        """Test valid UUID4 string."""
        assert is_valid_uuid("550e8400-e29b-41d4-a716-446655440000") is True

    def test_valid_uuid1(self):
        """Test valid UUID1 string."""
        assert is_valid_uuid("6ba7b810-9dad-11d1-80b4-00c04fd430c8") is True

    def test_valid_uuid_uppercase(self):
        """Test valid UUID with uppercase letters."""
        assert is_valid_uuid("550E8400-E29B-41D4-A716-446655440000") is True

    def test_invalid_uuid_format(self):
        """Test invalid UUID format."""
        assert is_valid_uuid("not-a-uuid") is False

    def test_invalid_uuid_too_short(self):
        """Test UUID string that's too short."""
        assert is_valid_uuid("12345") is False

    def test_invalid_uuid_empty_string(self):
        """Test empty string."""
        assert is_valid_uuid("") is False

    def test_invalid_uuid_none(self):
        """Test None value."""
        assert is_valid_uuid(None) is False

    def test_invalid_uuid_wrong_length(self):
        """Test UUID with wrong length."""
        assert is_valid_uuid("550e8400-e29b-41d4-a716") is False

    def test_invalid_uuid_extra_characters(self):
        """Test UUID with extra characters."""
        assert is_valid_uuid("550e8400-e29b-41d4-a716-446655440000-extra") is False


class TestGetSafeHeaders:
    """Tests for get_safe_headers function."""

    def test_filters_authorization_header(self):
        """Test Authorization header is filtered."""
        headers = {"Authorization": "Bearer token123", "Content-Type": "application/json"}
        result = get_safe_headers(headers)
        assert "Authorization" not in result
        assert "Content-Type" in result

    def test_filters_cookie_headers(self):
        """Test Cookie and Set-Cookie headers are filtered."""
        headers = {
            "Cookie": "session=abc123",
            "Set-Cookie": "token=xyz",
            "Content-Type": "application/json",
        }
        result = get_safe_headers(headers)
        assert "Cookie" not in result
        assert "Set-Cookie" not in result
        assert "Content-Type" in result

    def test_filters_api_key_headers(self):
        """Test API key headers are filtered."""
        headers = {
            "X-Api-Key": "secret-key",
            "X-Auth-Token": "auth-token",
            "X-Sensor-Token": "sensor-token",
            "Content-Type": "application/json",
        }
        result = get_safe_headers(headers)
        assert "X-Api-Key" not in result
        assert "X-Auth-Token" not in result
        assert "X-Sensor-Token" not in result
        assert "Content-Type" in result

    def test_case_insensitive_filtering(self):
        """Test header filtering is case-insensitive."""
        headers = {
            "authorization": "Bearer token",
            "AUTHORIZATION": "Bearer token2",
            "Authorization": "Bearer token3",
        }
        result = get_safe_headers(headers)
        assert "authorization" not in result
        assert "AUTHORIZATION" not in result
        assert "Authorization" not in result

    def test_preserves_safe_headers(self):
        """Test safe headers are preserved."""
        headers = {
            "Content-Type": "application/json",
            "User-Agent": "TestClient/1.0",
            "Accept": "application/json",
        }
        result = get_safe_headers(headers)
        assert result == headers

    def test_handles_multidict(self):
        """Test handling of MultiDict-like objects."""
        # Simulate aiohttp MultiDictProxy
        class MockMultiDict:
            def items(self):
                return [
                    ("Authorization", "Bearer token"),
                    ("Content-Type", "application/json"),
                ]

        headers = MockMultiDict()
        result = get_safe_headers(headers)
        assert "Authorization" not in result
        assert "Content-Type" in result

    def test_handles_list_values(self):
        """Test handling of headers with list values."""
        headers = {
            "Accept": ["application/json", "text/html"],
            "Content-Type": "application/json",
        }
        result = get_safe_headers(headers)
        assert "Accept" in result
        assert result["Accept"] == ["application/json", "text/html"]

    def test_handles_single_list_value(self):
        """Test handling of headers with single-item list."""
        headers = {"Accept": ["application/json"]}
        result = get_safe_headers(headers)
        assert result["Accept"] == "application/json"

    def test_empty_headers(self):
        """Test empty headers dict."""
        result = get_safe_headers({})
        assert result == {}

    def test_all_sensitive_headers(self):
        """Test when all headers are sensitive."""
        headers = {
            "Authorization": "Bearer token",
            "Cookie": "session=abc",
            "X-Api-Key": "key",
        }
        result = get_safe_headers(headers)
        assert result == {}


class TestTraceMiddlewareCreation:
    """Tests for create_trace_middleware function."""

    def test_creates_middleware(self):
        """Test create_trace_middleware returns middleware."""
        middleware = create_trace_middleware("test-service")
        assert middleware is not None
        assert callable(middleware)

    def test_middleware_has_service_name(self):
        """Test middleware is configured with service name."""
        middleware = create_trace_middleware("my-service")
        # Service name should be stored in closure
        assert middleware is not None


class TestTraceMiddlewareExecution:
    """Tests for trace middleware execution."""

    @pytest.mark.asyncio
    async def test_adds_trace_id_to_request(self):
        """Test middleware adds trace_id to request."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        await middleware(request, handler)

        assert "trace_id" in request
        assert "request_id" in request

    @pytest.mark.asyncio
    async def test_adds_request_id_to_request(self):
        """Test middleware adds request_id to request."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        await middleware(request, handler)

        assert request["request_id"] is not None

    @pytest.mark.asyncio
    async def test_uses_existing_trace_id(self):
        """Test middleware uses existing trace_id from headers."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        existing_trace_id = "550e8400-e29b-41d4-a716-446655440000"
        request = MagicMock()
        request.headers = {TRACE_ID_HEADER: existing_trace_id}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        await middleware(request, handler)

        assert request["trace_id"] == existing_trace_id

    @pytest.mark.asyncio
    async def test_uses_existing_request_id(self):
        """Test middleware uses existing request_id from headers."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        existing_request_id = "6ba7b810-9dad-11d1-80b4-00c04fd430c8"
        request = MagicMock()
        request.headers = {REQUEST_ID_HEADER: existing_request_id}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        await middleware(request, handler)

        assert request["request_id"] == existing_request_id

    @pytest.mark.asyncio
    async def test_generates_new_trace_id_if_invalid(self):
        """Test middleware generates new trace_id if invalid."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {TRACE_ID_HEADER: "invalid-uuid"}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        await middleware(request, handler)

        assert request["trace_id"] != "invalid-uuid"
        assert is_valid_uuid(request["trace_id"])

    @pytest.mark.asyncio
    async def test_adds_trace_id_to_response_headers(self):
        """Test middleware adds trace_id to response headers."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        response = await middleware(request, handler)

        assert TRACE_ID_HEADER in response.headers
        assert REQUEST_ID_HEADER in response.headers

    @pytest.mark.asyncio
    async def test_calls_handler(self):
        """Test middleware calls the handler."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        await middleware(request, handler)

        handler.assert_called_once_with(request)


class TestTraceMiddlewareLogging:
    """Tests for trace middleware logging behavior."""

    @pytest.mark.asyncio
    async def test_logs_incoming_request(self):
        """Test middleware logs incoming request."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        with patch("backend_common.middleware.trace.logger") as mock_logger:
            await middleware(request, handler)

            mock_logger.info.assert_called()
            calls = [str(call) for call in mock_logger.info.call_args_list]
            assert any("Incoming request" in str(call) for call in calls)

    @pytest.mark.asyncio
    async def test_logs_successful_response(self):
        """Test middleware logs successful response."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        with patch("backend_common.middleware.trace.logger") as mock_logger:
            await middleware(request, handler)

            calls = [str(call) for call in mock_logger.info.call_args_list]
            assert any("Request completed" in str(call) for call in calls)

    @pytest.mark.asyncio
    async def test_logs_error_status_as_warn(self):
        """Test middleware logs 4xx/5xx as warn."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=404))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        with patch("backend_common.middleware.trace.logger") as mock_logger:
            await middleware(request, handler)

            mock_logger.warn.assert_called()
            calls = [str(call) for call in mock_logger.warn.call_args_list]
            assert any("error status" in str(call) for call in calls)

    @pytest.mark.asyncio
    async def test_logs_http_exception(self):
        """Test middleware logs HTTP exceptions."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(side_effect=web.HTTPBadRequest(reason="Bad request"))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        with patch("backend_common.middleware.trace.logger") as mock_logger:
            with pytest.raises(web.HTTPBadRequest):
                await middleware(request, handler)

            mock_logger.warn.assert_called()

    @pytest.mark.asyncio
    async def test_logs_general_exception(self):
        """Test middleware logs general exceptions."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(side_effect=ValueError("Test error"))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        with patch("backend_common.middleware.trace.logger") as mock_logger:
            with pytest.raises(ValueError):
                await middleware(request, handler)

            mock_logger.error.assert_called()

    @pytest.mark.asyncio
    async def test_logs_request_duration(self):
        """Test middleware logs request duration."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        with patch("backend_common.middleware.trace.logger") as mock_logger:
            await middleware(request, handler)

            calls = mock_logger.info.call_args_list
            # Check that duration_ms is logged
            response_log = calls[-1]
            assert "duration_ms" in str(response_log)

    @pytest.mark.asyncio
    async def test_clears_context_vars_after_request(self):
        """Test middleware clears structlog context vars."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        with patch("backend_common.middleware.trace.structlog.contextvars") as mock_context:
            await middleware(request, handler)

            mock_context.clear_contextvars.assert_called()


class TestTraceMiddlewareHeaders:
    """Tests for trace middleware header handling."""

    @pytest.mark.asyncio
    async def test_filters_sensitive_headers_from_logging(self):
        """Test middleware filters sensitive headers from logging."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {
            "Authorization": "Bearer secret",
            "Content-Type": "application/json",
        }
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        with patch("backend_common.middleware.trace.logger") as mock_logger:
            await middleware(request, handler)

            # Check that logged headers don't contain Authorization
            calls = mock_logger.info.call_args_list
            incoming_log = calls[0]
            assert "Authorization" not in str(incoming_log)

    @pytest.mark.asyncio
    async def test_includes_content_length_when_present(self):
        """Test middleware includes content_length when present."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "POST"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = 1024

        with patch("backend_common.middleware.trace.logger") as mock_logger:
            await middleware(request, handler)

            calls = mock_logger.info.call_args_list
            incoming_log = str(calls[0])
            assert "content_length" in incoming_log


class TestTraceMiddlewareEdgeCases:
    """Tests for trace middleware edge cases."""

    @pytest.mark.asyncio
    async def test_handles_missing_remote_attribute(self):
        """Test middleware handles missing remote attribute."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.content_length = None
        # Remove remote attribute
        del request.remote

        # Should not raise
        await middleware(request, handler)

    @pytest.mark.asyncio
    async def test_handles_empty_query_string(self):
        """Test middleware handles empty query string."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        # Should not raise
        await middleware(request, handler)

    @pytest.mark.asyncio
    async def test_handles_non_empty_query_string(self):
        """Test middleware handles non-empty query string."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = "param=value"
        request.remote = "127.0.0.1"
        request.content_length = None

        # Should not raise
        await middleware(request, handler)

    @pytest.mark.asyncio
    async def test_handles_missing_content_length(self):
        """Test middleware handles missing content_length."""
        middleware = create_trace_middleware("test-service")
        handler = AsyncMock(return_value=web.Response(status=200))

        request = MagicMock()
        request.headers = {}
        request.method = "GET"
        request.path = "/test"
        request.url = "http://test.com/test"
        request.query_string = ""
        request.remote = "127.0.0.1"
        request.content_length = None

        # Should not raise
        await middleware(request, handler)
