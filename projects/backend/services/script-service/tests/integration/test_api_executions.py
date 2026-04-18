"""Integration tests for execution endpoints."""
from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from tests.integration.conftest import (
    SAMPLE_SCRIPT_PAYLOAD,
    make_executor_headers,
    make_manager_headers,
    make_no_perm_headers,
    make_superadmin_headers,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_manage_only_headers(
    user_id: str = "550e8400-e29b-41d4-a716-446655440004",
) -> dict[str, str]:
    """Headers for a user with scripts.manage but without scripts.execute."""
    return {
        "X-User-Id": user_id,
        "X-User-System-Permissions": "scripts.manage",
        "X-User-Permissions": "",
        "X-User-Is-Superadmin": "false",
    }


def make_view_logs_headers(
    user_id: str = "550e8400-e29b-41d4-a716-446655440005",
) -> dict[str, str]:
    """Headers for a user with scripts.view_logs but without scripts.execute."""
    return {
        "X-User-Id": user_id,
        "X-User-System-Permissions": "scripts.view_logs",
        "X-User-Permissions": "",
        "X-User-Is-Superadmin": "false",
    }


@pytest.fixture
def mock_rabbitmq():
    """Mock aio_pika to avoid real RabbitMQ connections."""
    mock_channel = MagicMock()
    mock_channel.is_closed = False
    mock_exchange = MagicMock()
    mock_exchange.publish = AsyncMock()
    mock_channel.default_exchange = mock_exchange

    mock_connection = MagicMock()
    mock_connection.is_closed = False
    mock_connection.channel = AsyncMock(return_value=mock_channel)

    with patch("aio_pika.connect_robust", new=AsyncMock(return_value=mock_connection)):
        yield mock_exchange


async def _create_script(client, name: str = "exec-test-script") -> str:
    """Create a script and return its id."""
    resp = await client.post(
        "/api/v1/scripts",
        json={**SAMPLE_SCRIPT_PAYLOAD, "name": name},
        headers=make_manager_headers(),
    )
    assert resp.status == 201
    return (await resp.json())["id"]


async def _execute_script(client, script_id: str) -> dict:
    """POST execute and return response body."""
    resp = await client.post(
        f"/api/v1/scripts/{script_id}/execute",
        json={},
        headers=make_executor_headers(),
    )
    return resp, await resp.json()


# ===========================================================================
# TestExecuteScript
# ===========================================================================

class TestExecuteScript:
    async def test_execute_script_success_returns_202_with_fields(
        self, service_client, mock_rabbitmq
    ):
        script_id = await _create_script(service_client, "exec-success-script")
        resp = await service_client.post(
            f"/api/v1/scripts/{script_id}/execute",
            json={},
            headers=make_executor_headers(),
        )
        assert resp.status == 202
        data = await resp.json()
        assert "id" in data
        assert data["status"] == "pending"
        assert data["script_id"] == script_id

    async def test_execute_script_not_found_returns_404(
        self, service_client, mock_rabbitmq
    ):
        resp = await service_client.post(
            "/api/v1/scripts/00000000-0000-0000-0000-000000000000/execute",
            json={},
            headers=make_executor_headers(),
        )
        assert resp.status == 404

    async def test_execute_requires_execute_permission_no_perm_returns_403(
        self, service_client, mock_rabbitmq
    ):
        script_id = await _create_script(service_client, "exec-perm-script")
        resp = await service_client.post(
            f"/api/v1/scripts/{script_id}/execute",
            json={},
            headers=make_no_perm_headers(),
        )
        assert resp.status == 403

    async def test_execute_missing_permission_manage_only_returns_403(
        self, service_client, mock_rabbitmq
    ):
        script_id = await _create_script(service_client, "exec-manage-only-script")
        resp = await service_client.post(
            f"/api/v1/scripts/{script_id}/execute",
            json={},
            headers=make_manage_only_headers(),
        )
        assert resp.status == 403

    async def test_execute_invalid_script_id_returns_400(
        self, service_client, mock_rabbitmq
    ):
        resp = await service_client.post(
            "/api/v1/scripts/not-a-uuid/execute",
            json={},
            headers=make_executor_headers(),
        )
        assert resp.status == 400

    async def test_execute_publishes_to_rabbitmq(
        self, service_client, mock_rabbitmq
    ):
        script_id = await _create_script(service_client, "exec-publish-script")
        resp = await service_client.post(
            f"/api/v1/scripts/{script_id}/execute",
            json={},
            headers=make_executor_headers(),
        )
        assert resp.status == 202
        mock_rabbitmq.publish.assert_awaited_once()


# ===========================================================================
# TestCancelExecution
# ===========================================================================

class TestCancelExecution:
    async def test_cancel_execution_success_returns_200_with_cancelled_status(
        self, service_client, mock_rabbitmq
    ):
        script_id = await _create_script(service_client, "cancel-success-script")
        exec_resp = await service_client.post(
            f"/api/v1/scripts/{script_id}/execute",
            json={},
            headers=make_executor_headers(),
        )
        assert exec_resp.status == 202
        execution_id = (await exec_resp.json())["id"]

        cancel_resp = await service_client.post(
            f"/api/v1/executions/{execution_id}/cancel",
            headers=make_executor_headers(),
        )
        assert cancel_resp.status == 200
        data = await cancel_resp.json()
        assert data["status"] == "cancelled"

    async def test_cancel_not_found_returns_404(
        self, service_client, mock_rabbitmq
    ):
        resp = await service_client.post(
            "/api/v1/executions/00000000-0000-0000-0000-000000000000/cancel",
            headers=make_executor_headers(),
        )
        assert resp.status == 404

    async def test_cancel_requires_execute_permission_no_perm_returns_403(
        self, service_client, mock_rabbitmq
    ):
        script_id = await _create_script(service_client, "cancel-perm-script")
        exec_resp = await service_client.post(
            f"/api/v1/scripts/{script_id}/execute",
            json={},
            headers=make_executor_headers(),
        )
        assert exec_resp.status == 202
        execution_id = (await exec_resp.json())["id"]

        cancel_resp = await service_client.post(
            f"/api/v1/executions/{execution_id}/cancel",
            headers=make_no_perm_headers(),
        )
        assert cancel_resp.status == 403


# ===========================================================================
# TestListExecutions
# ===========================================================================

class TestListExecutions:
    async def test_list_executions_empty_returns_200_with_empty_list(
        self, service_client
    ):
        resp = await service_client.get(
            "/api/v1/executions",
            headers=make_executor_headers(),
        )
        assert resp.status == 200
        data = await resp.json()
        assert "executions" in data
        assert isinstance(data["executions"], list)

    async def test_list_executions_returns_created_execution(
        self, service_client, mock_rabbitmq
    ):
        script_id = await _create_script(service_client, "list-returns-script")
        exec_resp = await service_client.post(
            f"/api/v1/scripts/{script_id}/execute",
            json={},
            headers=make_executor_headers(),
        )
        assert exec_resp.status == 202
        execution_id = (await exec_resp.json())["id"]

        list_resp = await service_client.get(
            "/api/v1/executions",
            headers=make_executor_headers(),
        )
        assert list_resp.status == 200
        data = await list_resp.json()
        ids = [e["id"] for e in data["executions"]]
        assert execution_id in ids

    async def test_list_filter_by_status_returns_only_pending(
        self, service_client, mock_rabbitmq
    ):
        script_id = await _create_script(service_client, "list-filter-status-script")
        exec_resp = await service_client.post(
            f"/api/v1/scripts/{script_id}/execute",
            json={},
            headers=make_executor_headers(),
        )
        assert exec_resp.status == 202

        list_resp = await service_client.get(
            "/api/v1/executions?status=pending",
            headers=make_executor_headers(),
        )
        assert list_resp.status == 200
        data = await list_resp.json()
        for execution in data["executions"]:
            assert execution["status"] == "pending"

    async def test_list_filter_by_script_id_returns_only_matching(
        self, service_client, mock_rabbitmq
    ):
        script_id = await _create_script(service_client, "list-filter-scriptid-script")
        exec_resp = await service_client.post(
            f"/api/v1/scripts/{script_id}/execute",
            json={},
            headers=make_executor_headers(),
        )
        assert exec_resp.status == 202

        list_resp = await service_client.get(
            f"/api/v1/executions?script_id={script_id}",
            headers=make_executor_headers(),
        )
        assert list_resp.status == 200
        data = await list_resp.json()
        assert len(data["executions"]) >= 1
        for execution in data["executions"]:
            assert execution["script_id"] == script_id

    async def test_list_no_permission_returns_403(self, service_client):
        resp = await service_client.get(
            "/api/v1/executions",
            headers=make_no_perm_headers(),
        )
        assert resp.status == 403

    async def test_list_executor_can_view_returns_200(self, service_client):
        resp = await service_client.get(
            "/api/v1/executions",
            headers=make_executor_headers(),
        )
        assert resp.status == 200

    async def test_list_view_logs_can_view_returns_200(self, service_client):
        resp = await service_client.get(
            "/api/v1/executions",
            headers=make_view_logs_headers(),
        )
        assert resp.status == 200


# ===========================================================================
# TestGetExecution
# ===========================================================================

class TestGetExecution:
    async def test_get_execution_by_id_returns_200_with_all_fields(
        self, service_client, mock_rabbitmq
    ):
        script_id = await _create_script(service_client, "get-exec-by-id-script")
        exec_resp = await service_client.post(
            f"/api/v1/scripts/{script_id}/execute",
            json={},
            headers=make_executor_headers(),
        )
        assert exec_resp.status == 202
        execution_id = (await exec_resp.json())["id"]

        resp = await service_client.get(
            f"/api/v1/executions/{execution_id}",
            headers=make_executor_headers(),
        )
        assert resp.status == 200
        data = await resp.json()
        assert data["id"] == execution_id
        assert data["script_id"] == script_id
        assert "status" in data
        assert "parameters" in data
        assert "requested_by" in data
        assert "created_at" in data
        assert "updated_at" in data

    async def test_get_execution_not_found_returns_404(self, service_client):
        resp = await service_client.get(
            "/api/v1/executions/00000000-0000-0000-0000-000000000000",
            headers=make_executor_headers(),
        )
        assert resp.status == 404

    async def test_get_no_permission_returns_403(self, service_client):
        resp = await service_client.get(
            "/api/v1/executions/00000000-0000-0000-0000-000000000001",
            headers=make_no_perm_headers(),
        )
        assert resp.status == 403
