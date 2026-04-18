"""Unit tests for backend_common.aiohttp_app module."""
from __future__ import annotations

from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from aiohttp import web
from aiohttp_cors import CorsConfig

from backend_common.aiohttp_app import (
    _ALLOWED_HEADERS,
    _ALLOWED_METHODS,
    _EXPOSED_HEADERS,
    add_cors_to_routes,
    add_healthcheck,
    add_openapi_spec,
    create_base_app,
    extract_bearer_token,
    read_json,
)


class TestConstants:
    """Tests for module constants."""

    def test_allowed_headers_is_tuple(self):
        """Test _ALLOWED_HEADERS is a tuple."""
        assert isinstance(_ALLOWED_HEADERS, tuple)
        assert len(_ALLOWED_HEADERS) > 0

    def test_allowed_methods_includes_all_http_methods(self):
        """Test _ALLOWED_METHODS includes all HTTP methods."""
        assert "GET" in _ALLOWED_METHODS
        assert "POST" in _ALLOWED_METHODS
        assert "PUT" in _ALLOWED_METHODS
        assert "PATCH" in _ALLOWED_METHODS
        assert "DELETE" in _ALLOWED_METHODS
        assert "OPTIONS" in _ALLOWED_METHODS
        assert "HEAD" in _ALLOWED_METHODS

    def test_exposed_headers_includes_trace_headers(self):
        """Test _EXPOSED_HEADERS includes trace headers."""
        assert "X-Trace-Id" in _EXPOSED_HEADERS
        assert "X-Request-Id" in _EXPOSED_HEADERS

    def test_allowed_headers_includes_auth(self):
        """Test _ALLOWED_HEADERS includes Authorization."""
        assert "Authorization" in _ALLOWED_HEADERS
        assert "Content-Type" in _ALLOWED_HEADERS


class TestCreateBaseApp:
    """Tests for create_base_app function."""

    def test_returns_app_and_cors(self):
        """Test create_base_app returns app and cors config."""
        mock_settings = MagicMock()
        mock_settings.app_name = "test-service"
        mock_settings.env = "development"
        mock_settings.cors_allowed_origins = ["http://localhost:3000"]

        app, cors = create_base_app(mock_settings)

        assert isinstance(app, web.Application)
        assert cors is not None

    def test_app_has_trace_middleware(self):
        """Test created app has trace middleware."""
        mock_settings = MagicMock()
        mock_settings.app_name = "test-service"
        mock_settings.env = "development"
        mock_settings.cors_allowed_origins = []

        app, _ = create_base_app(mock_settings)

        assert len(app.middlewares) > 0

    def test_cors_configured_for_allowed_origins(self):
        """Test CORS is configured for allowed origins."""
        mock_settings = MagicMock()
        mock_settings.app_name = "test-service"
        mock_settings.env = "development"
        mock_settings.cors_allowed_origins = [
            "http://localhost:3000",
            "https://example.com",
        ]

        app, cors = create_base_app(mock_settings)

        assert isinstance(cors, CorsConfig)

    def test_cors_with_empty_origins(self):
        """Test CORS works with empty origins list."""
        mock_settings = MagicMock()
        mock_settings.app_name = "test-service"
        mock_settings.env = "development"
        mock_settings.cors_allowed_origins = []

        app, cors = create_base_app(mock_settings)

        assert isinstance(cors, CorsConfig)

    def test_app_name_passed_to_middleware(self):
        """Test app_name is used in trace middleware."""
        mock_settings = MagicMock()
        mock_settings.app_name = "my-test-service"
        mock_settings.env = "development"
        mock_settings.cors_allowed_origins = []

        with patch("backend_common.aiohttp_app.create_trace_middleware") as mock_create:
            create_base_app(mock_settings)

            mock_create.assert_called_once_with("my-test-service")


class TestAddHealthcheck:
    """Tests for add_healthcheck function."""

    def test_registers_health_endpoint(self):
        """Test healthcheck endpoint is registered."""
        app = web.Application()
        mock_settings = MagicMock()
        mock_settings.app_name = "test-service"
        mock_settings.env = "development"

        add_healthcheck(app, mock_settings)

        # Check route exists
        routes = [str(route) for route in app.router.routes()]
        assert any("/health" in str(r) for r in app.router.routes())

    @pytest.mark.asyncio
    async def test_healthcheck_returns_ok_status(self):
        """Test healthcheck returns OK status."""
        app = web.Application()
        mock_settings = MagicMock()
        mock_settings.app_name = "test-service"
        mock_settings.env = "development"

        add_healthcheck(app, mock_settings)

        # Get the handler
        for route in app.router.routes():
            if "/health" in str(route):
                handler = route.handler
                break

        request = MagicMock()
        response = await handler(request)

        assert response.status == 200
        data = await response.json()
        assert data["status"] == "ok"
        assert data["service"] == "test-service"
        assert data["env"] == "development"

    @pytest.mark.asyncio
    async def test_healthcheck_ignores_request(self):
        """Test healthcheck handler ignores request."""
        app = web.Application()
        mock_settings = MagicMock()
        mock_settings.app_name = "test-service"
        mock_settings.env = "production"

        add_healthcheck(app, mock_settings)

        # Get the handler
        for route in app.router.routes():
            if "/health" in str(route):
                handler = route.handler
                break

        # Should work with any request
        request = MagicMock()
        response = await handler(request)

        assert response.status == 200


class TestAddOpenapiSpec:
    """Tests for add_openapi_spec function."""

    def test_registers_openapi_endpoint(self):
        """Test openapi spec endpoint is registered."""
        app = web.Application()
        mock_path = Path("/tmp/openapi.yaml")

        add_openapi_spec(app, mock_path)

        # Check route exists
        routes = list(app.router.routes())
        assert any("/openapi.yaml" in str(r) for r in routes)

    @pytest.mark.asyncio
    async def test_openapi_handler_returns_file(self):
        """Test openapi handler returns FileResponse."""
        app = web.Application()

        # Create a temporary file
        import tempfile
        with tempfile.NamedTemporaryFile(mode='w', suffix='.yaml', delete=False) as f:
            f.write("openapi: 3.0.0\ninfo:\n  title: Test API\n")
            temp_path = Path(f.name)

        try:
            add_openapi_spec(app, temp_path)

            # Get the handler
            for route in app.router.routes():
                if "/openapi.yaml" in str(route):
                    handler = route.handler
                    break

            request = MagicMock()
            response = await handler(request)

            assert response is not None
        finally:
            temp_path.unlink()

    def test_openapi_path_can_be_any_path(self):
        """Test openapi spec accepts any Path."""
        app = web.Application()

        # Should not raise even if file doesn't exist
        add_openapi_spec(app, Path("/nonexistent/openapi.yaml"))


class TestAddCorsToRoutes:
    """Tests for add_cors_to_routes function."""

    def test_applies_cors_to_routes(self):
        """Test CORS is applied to routes."""
        app = web.Application()
        mock_cors = MagicMock(spec=CorsConfig)

        # Add some routes
        app.router.add_get("/test", MagicMock())
        app.router.add_post("/test", MagicMock())

        add_cors_to_routes(app, mock_cors)

        # CORS should be called for each route
        assert mock_cors.add.call_count >= 2

    def test_handles_empty_routes(self):
        """Test CORS handles empty routes."""
        app = web.Application()
        mock_cors = MagicMock(spec=CorsConfig)

        # Should not raise
        add_cors_to_routes(app, mock_cors)

    def test_copies_routes_list(self):
        """Test routes are copied before iteration."""
        app = web.Application()
        mock_cors = MagicMock(spec=CorsConfig)

        app.router.add_get("/test", MagicMock())

        # Should not raise
        add_cors_to_routes(app, mock_cors)


class TestReadJson:
    """Tests for read_json function."""

    @pytest.mark.asyncio
    async def test_parses_valid_json(self):
        """Test read_json parses valid JSON."""
        request = AsyncMock()
        request.json = AsyncMock(return_value={"key": "value"})

        result = await read_json(request)

        assert result == {"key": "value"}
        request.json.assert_awaited_once()

    @pytest.mark.asyncio
    async def test_parses_empty_object(self):
        """Test read_json parses empty object."""
        request = AsyncMock()
        request.json = AsyncMock(return_value={})

        result = await read_json(request)

        assert result == {}

    @pytest.mark.asyncio
    async def test_raises_on_invalid_json(self):
        """Test read_json raises HTTPBadRequest on invalid JSON."""
        request = AsyncMock()
        request.json = AsyncMock(side_effect=ValueError("Invalid JSON"))

        with pytest.raises(web.HTTPBadRequest, match="Invalid JSON payload"):
            await read_json(request)

    @pytest.mark.asyncio
    async def test_raises_on_non_dict_json(self):
        """Test read_json raises HTTPBadRequest on non-dict JSON."""
        request = AsyncMock()
        request.json = AsyncMock(return_value=["array", "not", "object"])

        with pytest.raises(web.HTTPBadRequest, match="JSON body must be an object"):
            await read_json(request)

    @pytest.mark.asyncio
    async def test_raises_on_null_json(self):
        """Test read_json raises HTTPBadRequest on null JSON."""
        request = AsyncMock()
        request.json = AsyncMock(return_value=None)

        with pytest.raises(web.HTTPBadRequest, match="JSON body must be an object"):
            await read_json(request)

    @pytest.mark.asyncio
    async def test_raises_on_string_json(self):
        """Test read_json raises HTTPBadRequest on string JSON."""
        request = AsyncMock()
        request.json = AsyncMock(return_value="string value")

        with pytest.raises(web.HTTPBadRequest, match="JSON body must be an object"):
            await read_json(request)

    @pytest.mark.asyncio
    async def test_raises_on_number_json(self):
        """Test read_json raises HTTPBadRequest on number JSON."""
        request = AsyncMock()
        request.json = AsyncMock(return_value=42)

        with pytest.raises(web.HTTPBadRequest, match="JSON body must be an object"):
            await read_json(request)

    @pytest.mark.asyncio
    async def test_parses_nested_object(self):
        """Test read_json parses nested objects."""
        request = AsyncMock()
        request.json = AsyncMock(return_value={
            "nested": {"key": "value"},
            "array": [1, 2, 3],
        })

        result = await read_json(request)

        assert result["nested"]["key"] == "value"
        assert result["array"] == [1, 2, 3]


class TestExtractBearerToken:
    """Tests for extract_bearer_token function."""

    def test_extracts_valid_token(self):
        """Test extract_bearer_token extracts valid token."""
        request = MagicMock()
        request.headers = {"Authorization": "Bearer valid-token-123"}

        token = extract_bearer_token(request)

        assert token == "valid-token-123"

    def test_extracts_token_with_spaces(self):
        """Test extract_bearer_token handles extra spaces."""
        request = MagicMock()
        request.headers = {"Authorization": "Bearer   token-with-spaces   "}

        token = extract_bearer_token(request)

        assert token == "token-with-spaces"

    def test_raises_on_missing_header(self):
        """Test extract_bearer_token raises on missing header."""
        request = MagicMock()
        request.headers = {}

        with pytest.raises(web.HTTPUnauthorized, match="Authorization token is required"):
            extract_bearer_token(request)

    def test_raises_on_empty_header(self):
        """Test extract_bearer_token raises on empty header."""
        request = MagicMock()
        request.headers = {"Authorization": ""}

        with pytest.raises(web.HTTPUnauthorized, match="Authorization token is required"):
            extract_bearer_token(request)

    def test_raises_on_non_bearer_header(self):
        """Test extract_bearer_token raises on non-Bearer header."""
        request = MagicMock()
        request.headers = {"Authorization": "Basic dXNlcjpwYXNz"}

        with pytest.raises(web.HTTPUnauthorized, match="Authorization token is required"):
            extract_bearer_token(request)

    def test_raises_on_bearer_only(self):
        """Test extract_bearer_token raises on 'Bearer' only."""
        request = MagicMock()
        request.headers = {"Authorization": "Bearer"}

        with pytest.raises(web.HTTPUnauthorized, match="Authorization token is required"):
            extract_bearer_token(request)

    def test_raises_on_bearer_with_only_spaces(self):
        """Test extract_bearer_token raises on 'Bearer' with only spaces."""
        request = MagicMock()
        request.headers = {"Authorization": "Bearer   "}

        with pytest.raises(web.HTTPUnauthorized, match="Authorization token is required"):
            extract_bearer_token(request)

    def test_handles_lowercase_bearer(self):
        """Test extract_bearer_token requires exact 'Bearer' prefix."""
        request = MagicMock()
        request.headers = {"Authorization": "bearer lowercase-token"}

        with pytest.raises(web.HTTPUnauthorized, match="Authorization token is required"):
            extract_bearer_token(request)

    def test_extracts_complex_token(self):
        """Test extract_bearer_token extracts complex JWT-like token."""
        complex_token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.dozjgNryP4J3jVmNHl0w5N_XgL0n3I9PlFUP0THsR8U"
        request = MagicMock()
        request.headers = {"Authorization": f"Bearer {complex_token}"}

        token = extract_bearer_token(request)

        assert token == complex_token


class TestReadJsonIntegration:
    """Integration tests for read_json with aiohttp."""

    @pytest.mark.asyncio
    async def test_read_json_in_handler(self):
        """Test read_json works in aiohttp handler."""
        from aiohttp import web

        async def handler(request):
            data = await read_json(request)
            return web.json_response({"received": data})

        app = web.Application()
        app.router.add_post("/test", handler)

        # Simulate request
        from aiohttp.test_utils import make_mocked_request

        request = make_mocked_request(
            "POST",
            "/test",
            json={"key": "value"},
        )

        response = await handler(request)
        data = await response.json()

        assert data["received"] == {"key": "value"}


class TestExtractBearerTokenIntegration:
    """Integration tests for extract_bearer_token with aiohttp."""

    def test_extract_in_middleware(self):
        """Test extract_bearer_token works in middleware context."""
        from aiohttp.test_utils import make_mocked_request

        request = make_mocked_request(
            "GET",
            "/test",
            headers={"Authorization": "Bearer test-token"},
        )

        token = extract_bearer_token(request)
        assert token == "test-token"

    def test_extract_missing_token_in_middleware(self):
        """Test extract_bearer_token raises in middleware context."""
        from aiohttp.test_utils import make_mocked_request

        request = make_mocked_request(
            "GET",
            "/test",
            headers={},
        )

        with pytest.raises(web.HTTPUnauthorized):
            extract_bearer_token(request)


class TestAppConfiguration:
    """Tests for app configuration patterns."""

    def test_full_app_setup(self):
        """Test complete app setup pattern."""
        mock_settings = MagicMock()
        mock_settings.app_name = "full-test-service"
        mock_settings.env = "development"
        mock_settings.cors_allowed_origins = ["http://localhost:3000"]

        # Create base app
        app, cors = create_base_app(mock_settings)

        # Add healthcheck
        add_healthcheck(app, mock_settings)

        # Add openapi spec
        add_openapi_spec(app, Path("/tmp/openapi.yaml"))

        # Add CORS to routes
        add_cors_to_routes(app, cors)

        # Verify setup
        assert app is not None
        assert cors is not None
        assert len(app.router.routes()) > 0

    def test_minimal_app_setup(self):
        """Test minimal app setup."""
        mock_settings = MagicMock()
        mock_settings.app_name = "minimal-service"
        mock_settings.env = "production"
        mock_settings.cors_allowed_origins = []

        app, cors = create_base_app(mock_settings)

        assert app is not None
        assert cors is not None
