"""Health check tests."""
import pytest


@pytest.mark.asyncio
async def test_healthcheck(service_client):
    """Test health check endpoint."""
    response = await service_client.get("/health")
    assert response.status == 200
    payload = await response.json()
    assert payload["status"] == "ok"
    assert payload["service"] == "auth-service"

