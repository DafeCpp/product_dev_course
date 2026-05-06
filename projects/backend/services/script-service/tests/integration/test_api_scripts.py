"""Integration tests for /api/v1/scripts endpoints."""
from __future__ import annotations

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

async def _create_script(client, payload: dict | None = None, headers: dict | None = None):
    """POST /api/v1/scripts and return response."""
    return await client.post(
        "/api/v1/scripts",
        json=payload if payload is not None else SAMPLE_SCRIPT_PAYLOAD,
        headers=headers if headers is not None else make_manager_headers(),
    )


# ===========================================================================
# TestCreateScript
# ===========================================================================

class TestCreateScript:
    async def test_create_script_success_returns_201_with_all_fields(self, service_client):
        resp = await _create_script(service_client)
        assert resp.status == 201
        data = await resp.json()
        assert data["name"] == SAMPLE_SCRIPT_PAYLOAD["name"]
        assert data["target_service"] == SAMPLE_SCRIPT_PAYLOAD["target_service"]
        assert data["script_body"] == SAMPLE_SCRIPT_PAYLOAD["script_body"]
        assert data["script_type"] == "python"
        assert data["is_active"] is True
        assert "id" in data
        assert "created_at" in data
        assert "updated_at" in data

    async def test_create_script_missing_name_returns_400(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD}
        del payload["name"]
        resp = await _create_script(service_client, payload=payload)
        assert resp.status == 400

    async def test_create_script_missing_target_service_returns_400(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD}
        del payload["target_service"]
        resp = await _create_script(service_client, payload=payload)
        assert resp.status == 400

    async def test_create_script_missing_script_body_returns_400(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD}
        del payload["script_body"]
        resp = await _create_script(service_client, payload=payload)
        assert resp.status == 400

    async def test_create_script_invalid_script_type_returns_400(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD, "name": "bad-type-script", "script_type": "ruby"}
        resp = await _create_script(service_client, payload=payload)
        assert resp.status == 400

    async def test_create_requires_manage_permission_executor_gets_403(self, service_client):
        resp = await _create_script(service_client, headers=make_executor_headers())
        assert resp.status == 403

    async def test_create_no_permission_returns_403(self, service_client):
        resp = await _create_script(service_client, headers=make_no_perm_headers())
        assert resp.status == 403

    async def test_create_superadmin_can_create_returns_201(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD, "name": "superadmin-script"}
        resp = await _create_script(service_client, payload=payload, headers=make_superadmin_headers())
        assert resp.status == 201

    async def test_create_duplicate_name_returns_error(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD, "name": "duplicate-script"}
        resp1 = await _create_script(service_client, payload=payload)
        assert resp1.status == 201
        resp2 = await _create_script(service_client, payload=payload)
        assert resp2.status in (400, 409, 500)

    async def test_create_with_empty_string_name_returns_400(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD, "name": ""}
        resp = await _create_script(service_client, payload=payload)
        assert resp.status == 400

    async def test_create_with_null_name_returns_400(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD, "name": None}
        resp = await _create_script(service_client, payload=payload)
        assert resp.status == 400

    async def test_create_with_empty_target_service_returns_400(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD, "name": "empty-target", "target_service": ""}
        resp = await _create_script(service_client, payload=payload)
        assert resp.status == 400

    async def test_create_with_empty_script_body_returns_400(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD, "name": "empty-body", "script_body": ""}
        resp = await _create_script(service_client, payload=payload)
        assert resp.status == 400

    async def test_create_with_non_string_name_returns_400(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD, "name": 12345}
        resp = await _create_script(service_client, payload=payload)
        assert resp.status == 400

    async def test_create_omitted_description_persists_as_null(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD, "name": "no-description-script"}
        payload.pop("description", None)
        resp = await _create_script(service_client, payload=payload)
        assert resp.status == 201
        data = await resp.json()
        assert data["description"] is None

    async def test_create_omitted_parameters_schema_defaults_to_empty_dict(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD, "name": "no-params-script"}
        payload.pop("parameters_schema", None)
        resp = await _create_script(service_client, payload=payload)
        assert resp.status == 201
        data = await resp.json()
        assert data["parameters_schema"] == {}

    async def test_create_omitted_timeout_sec_uses_default(self, service_client):
        payload = {**SAMPLE_SCRIPT_PAYLOAD, "name": "default-timeout-script"}
        payload.pop("timeout_sec", None)
        resp = await _create_script(service_client, payload=payload)
        assert resp.status == 201

    async def test_create_missing_user_id_header_returns_401(self, service_client):
        # Sanity check that route runs extract_user before any business logic.
        resp = await service_client.post(
            "/api/v1/scripts",
            json=SAMPLE_SCRIPT_PAYLOAD,
            headers={"X-User-System-Permissions": "scripts.manage"},
        )
        assert resp.status == 401


# ===========================================================================
# TestListScripts
# ===========================================================================

class TestListScripts:
    async def test_list_scripts_manager_returns_200_with_list(self, service_client):
        await _create_script(service_client, payload={**SAMPLE_SCRIPT_PAYLOAD, "name": "list-test-script"})
        resp = await service_client.get("/api/v1/scripts", headers=make_manager_headers())
        assert resp.status == 200
        data = await resp.json()
        assert "scripts" in data
        assert isinstance(data["scripts"], list)

    async def test_list_scripts_executor_returns_200(self, service_client):
        resp = await service_client.get("/api/v1/scripts", headers=make_executor_headers())
        assert resp.status == 200

    async def test_list_scripts_no_permission_returns_403(self, service_client):
        resp = await service_client.get("/api/v1/scripts", headers=make_no_perm_headers())
        assert resp.status == 403

    async def test_list_scripts_filter_by_target_service_returns_only_matching(self, service_client):
        await _create_script(
            service_client,
            payload={**SAMPLE_SCRIPT_PAYLOAD, "name": "ts-filter-exp", "target_service": "experiment-service"},
        )
        await _create_script(
            service_client,
            payload={**SAMPLE_SCRIPT_PAYLOAD, "name": "ts-filter-auth", "target_service": "auth-service"},
        )
        resp = await service_client.get(
            "/api/v1/scripts?target_service=auth-service",
            headers=make_manager_headers(),
        )
        assert resp.status == 200
        data = await resp.json()
        for script in data["scripts"]:
            assert script["target_service"] == "auth-service"

    async def test_list_scripts_filter_by_is_active_false_returns_only_inactive(self, service_client):
        # Create and then soft-delete a script so there is at least one inactive
        create_resp = await _create_script(
            service_client,
            payload={**SAMPLE_SCRIPT_PAYLOAD, "name": "to-deactivate"},
        )
        assert create_resp.status == 201
        script_id = (await create_resp.json())["id"]
        del_resp = await service_client.delete(
            f"/api/v1/scripts/{script_id}",
            headers=make_manager_headers(),
        )
        assert del_resp.status == 204

        resp = await service_client.get(
            "/api/v1/scripts?is_active=false",
            headers=make_manager_headers(),
        )
        assert resp.status == 200
        data = await resp.json()
        for script in data["scripts"]:
            assert script["is_active"] is False

    async def test_list_scripts_pagination_limit_1_returns_single_entry(self, service_client):
        # Ensure at least two scripts exist
        for i in range(2):
            await _create_script(
                service_client,
                payload={**SAMPLE_SCRIPT_PAYLOAD, "name": f"pagination-script-{i}"},
            )
        resp = await service_client.get(
            "/api/v1/scripts?limit=1&offset=0",
            headers=make_manager_headers(),
        )
        assert resp.status == 200
        data = await resp.json()
        assert len(data["scripts"]) == 1

    async def test_list_scripts_invalid_limit_returns_400(self, service_client):
        resp = await service_client.get(
            "/api/v1/scripts?limit=abc",
            headers=make_manager_headers(),
        )
        assert resp.status == 400

    async def test_list_scripts_invalid_offset_returns_400(self, service_client):
        resp = await service_client.get(
            "/api/v1/scripts?offset=xyz",
            headers=make_manager_headers(),
        )
        assert resp.status == 400

    async def test_list_scripts_superadmin_returns_200(self, service_client):
        resp = await service_client.get(
            "/api/v1/scripts",
            headers=make_superadmin_headers(),
        )
        assert resp.status == 200

    async def test_list_scripts_is_active_true_returns_only_active(self, service_client):
        await _create_script(
            service_client,
            payload={**SAMPLE_SCRIPT_PAYLOAD, "name": "active-only-script"},
        )
        resp = await service_client.get(
            "/api/v1/scripts?is_active=true",
            headers=make_manager_headers(),
        )
        assert resp.status == 200
        data = await resp.json()
        for script in data["scripts"]:
            assert script["is_active"] is True


# ===========================================================================
# TestGetScript
# ===========================================================================

class TestGetScript:
    async def test_get_script_by_id_returns_200_with_correct_fields(self, service_client):
        create_resp = await _create_script(
            service_client,
            payload={**SAMPLE_SCRIPT_PAYLOAD, "name": "get-by-id-script"},
        )
        assert create_resp.status == 201
        script_id = (await create_resp.json())["id"]

        resp = await service_client.get(
            f"/api/v1/scripts/{script_id}",
            headers=make_manager_headers(),
        )
        assert resp.status == 200
        data = await resp.json()
        assert data["id"] == script_id
        assert data["name"] == "get-by-id-script"

    async def test_get_script_not_found_returns_404(self, service_client):
        resp = await service_client.get(
            "/api/v1/scripts/00000000-0000-0000-0000-000000000000",
            headers=make_manager_headers(),
        )
        assert resp.status == 404

    async def test_get_script_invalid_uuid_returns_400(self, service_client):
        resp = await service_client.get(
            "/api/v1/scripts/not-a-uuid",
            headers=make_manager_headers(),
        )
        assert resp.status == 400

    async def test_get_requires_permission_no_perm_returns_403(self, service_client):
        resp = await service_client.get(
            "/api/v1/scripts/00000000-0000-0000-0000-000000000001",
            headers=make_no_perm_headers(),
        )
        assert resp.status == 403

    async def test_get_executor_can_view(self, service_client):
        # scripts.execute alone (system_permissions) should grant read access.
        create_resp = await _create_script(
            service_client,
            payload={**SAMPLE_SCRIPT_PAYLOAD, "name": "executor-read-script"},
        )
        script_id = (await create_resp.json())["id"]
        resp = await service_client.get(
            f"/api/v1/scripts/{script_id}",
            headers=make_executor_headers(),
        )
        assert resp.status == 200

    async def test_get_superadmin_bypasses_permission_check(self, service_client):
        create_resp = await _create_script(
            service_client,
            payload={**SAMPLE_SCRIPT_PAYLOAD, "name": "superadmin-read-script"},
        )
        script_id = (await create_resp.json())["id"]
        resp = await service_client.get(
            f"/api/v1/scripts/{script_id}",
            headers=make_superadmin_headers(),
        )
        assert resp.status == 200


# ===========================================================================
# TestUpdateScript
# ===========================================================================

class TestUpdateScript:
    async def _make_script(self, client, name: str = "update-target-script") -> str:
        resp = await _create_script(
            client,
            payload={**SAMPLE_SCRIPT_PAYLOAD, "name": name},
        )
        assert resp.status == 201
        return (await resp.json())["id"]

    async def test_update_script_name_returns_200_with_new_name(self, service_client):
        script_id = await self._make_script(service_client, "update-name-script")
        resp = await service_client.patch(
            f"/api/v1/scripts/{script_id}",
            json={"name": "updated-name"},
            headers=make_manager_headers(),
        )
        assert resp.status == 200
        data = await resp.json()
        assert data["name"] == "updated-name"

    async def test_update_script_body_returns_200_with_new_body(self, service_client):
        script_id = await self._make_script(service_client, "update-body-script")
        new_body = "print('updated')"
        resp = await service_client.patch(
            f"/api/v1/scripts/{script_id}",
            json={"script_body": new_body},
            headers=make_manager_headers(),
        )
        assert resp.status == 200
        data = await resp.json()
        assert data["script_body"] == new_body

    async def test_update_invalid_script_type_returns_400(self, service_client):
        script_id = await self._make_script(service_client, "update-badtype-script")
        resp = await service_client.patch(
            f"/api/v1/scripts/{script_id}",
            json={"script_type": "cobol"},
            headers=make_manager_headers(),
        )
        assert resp.status == 400

    async def test_update_not_found_returns_404(self, service_client):
        resp = await service_client.patch(
            "/api/v1/scripts/00000000-0000-0000-0000-000000000000",
            json={"name": "ghost"},
            headers=make_manager_headers(),
        )
        assert resp.status == 404

    async def test_update_requires_manage_permission_executor_gets_403(self, service_client):
        script_id = await self._make_script(service_client, "update-perm-script")
        resp = await service_client.patch(
            f"/api/v1/scripts/{script_id}",
            json={"name": "sneaky-update"},
            headers=make_executor_headers(),
        )
        assert resp.status == 403

    async def test_update_unknown_field_silently_ignored(self, service_client):
        # Fields outside the whitelist must be dropped, not echoed back or persisted.
        script_id = await self._make_script(service_client, "update-unknown-script")
        resp = await service_client.patch(
            f"/api/v1/scripts/{script_id}",
            json={"name": "renamed", "totally_made_up_field": "boom"},
            headers=make_manager_headers(),
        )
        assert resp.status == 200
        data = await resp.json()
        assert data["name"] == "renamed"
        assert "totally_made_up_field" not in data

    async def test_update_partial_only_changes_specified_fields(self, service_client):
        script_id = await self._make_script(service_client, "update-partial-script")
        original = await (await service_client.get(
            f"/api/v1/scripts/{script_id}",
            headers=make_manager_headers(),
        )).json()

        resp = await service_client.patch(
            f"/api/v1/scripts/{script_id}",
            json={"description": "patched"},
            headers=make_manager_headers(),
        )
        assert resp.status == 200
        data = await resp.json()
        assert data["description"] == "patched"
        assert data["name"] == original["name"]
        assert data["script_body"] == original["script_body"]
        assert data["target_service"] == original["target_service"]

    async def test_update_script_type_to_valid_value_succeeds(self, service_client):
        script_id = await self._make_script(service_client, "update-stype-script")
        resp = await service_client.patch(
            f"/api/v1/scripts/{script_id}",
            json={"script_type": "bash"},
            headers=make_manager_headers(),
        )
        assert resp.status == 200
        data = await resp.json()
        assert data["script_type"] == "bash"

    async def test_update_invalid_uuid_returns_400(self, service_client):
        resp = await service_client.patch(
            "/api/v1/scripts/not-a-uuid",
            json={"name": "x"},
            headers=make_manager_headers(),
        )
        assert resp.status == 400


# ===========================================================================
# TestDeleteScript
# ===========================================================================

class TestDeleteScript:
    async def _make_script(self, client, name: str = "delete-target-script") -> str:
        resp = await _create_script(
            client,
            payload={**SAMPLE_SCRIPT_PAYLOAD, "name": name},
        )
        assert resp.status == 201
        return (await resp.json())["id"]

    async def test_delete_script_returns_204(self, service_client):
        script_id = await self._make_script(service_client, "delete-204-script")
        resp = await service_client.delete(
            f"/api/v1/scripts/{script_id}",
            headers=make_manager_headers(),
        )
        assert resp.status == 204

    async def test_delete_not_found_returns_404(self, service_client):
        resp = await service_client.delete(
            "/api/v1/scripts/00000000-0000-0000-0000-000000000000",
            headers=make_manager_headers(),
        )
        assert resp.status == 404

    async def test_delete_is_soft_script_appears_in_inactive_list(self, service_client):
        script_id = await self._make_script(service_client, "soft-delete-script")

        del_resp = await service_client.delete(
            f"/api/v1/scripts/{script_id}",
            headers=make_manager_headers(),
        )
        assert del_resp.status == 204

        # Soft-deleted script should appear when filtering is_active=false
        list_resp = await service_client.get(
            "/api/v1/scripts?is_active=false",
            headers=make_manager_headers(),
        )
        assert list_resp.status == 200
        ids = [(s["id"]) for s in (await list_resp.json())["scripts"]]
        assert script_id in ids

    async def test_delete_requires_manage_permission_executor_gets_403(self, service_client):
        script_id = await self._make_script(service_client, "delete-perm-script")
        resp = await service_client.delete(
            f"/api/v1/scripts/{script_id}",
            headers=make_executor_headers(),
        )
        assert resp.status == 403

    async def test_delete_invalid_uuid_returns_400(self, service_client):
        resp = await service_client.delete(
            "/api/v1/scripts/not-a-uuid",
            headers=make_manager_headers(),
        )
        assert resp.status == 400

    async def test_delete_superadmin_can_delete(self, service_client):
        script_id = await self._make_script(service_client, "delete-superadmin-script")
        resp = await service_client.delete(
            f"/api/v1/scripts/{script_id}",
            headers=make_superadmin_headers(),
        )
        assert resp.status == 204
