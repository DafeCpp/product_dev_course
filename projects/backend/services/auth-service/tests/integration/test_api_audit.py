"""Integration tests for audit log API endpoints."""
from __future__ import annotations

from uuid import uuid4

import pytest


class TestInternalAuditIngest:
    """Tests for POST /api/v1/internal/audit."""

    @pytest.mark.asyncio
    async def test_ingest_valid_entry(self, service_client):
        """POST /internal/audit writes and returns the entry."""
        actor_id = str(uuid4())
        resp = await service_client.post(
            "/api/v1/internal/audit",
            json={
                "actor_id": actor_id,
                "action": "experiment.create",
                "scope_type": "project",
                "scope_id": str(uuid4()),
                "target_type": "experiment",
                "target_id": str(uuid4()),
                "details": {"name": "My experiment"},
                "ip_address": "10.0.0.1",
                "user_agent": "test-service/1.0",
            },
        )
        assert resp.status == 201
        data = await resp.json()
        assert data["actor_id"] == actor_id
        assert data["action"] == "experiment.create"
        assert data["scope_type"] == "project"
        assert data["target_type"] == "experiment"
        assert data["details"] == {"name": "My experiment"}
        assert data["ip_address"] == "10.0.0.1"

    @pytest.mark.asyncio
    async def test_ingest_minimal_entry(self, service_client):
        """POST /internal/audit works with only required fields."""
        actor_id = str(uuid4())
        resp = await service_client.post(
            "/api/v1/internal/audit",
            json={
                "actor_id": actor_id,
                "action": "run.create",
                "scope_type": "system",
            },
        )
        assert resp.status == 201
        data = await resp.json()
        assert data["action"] == "run.create"
        assert data["scope_id"] is None
        assert data["target_type"] is None

    @pytest.mark.asyncio
    async def test_ingest_missing_required_fields(self, service_client):
        """POST /internal/audit returns 400 when required fields are missing."""
        resp = await service_client.post(
            "/api/v1/internal/audit",
            json={"action": "something.happened"},
        )
        assert resp.status == 400

    @pytest.mark.asyncio
    async def test_ingest_invalid_actor_id(self, service_client):
        """POST /internal/audit returns 400 for invalid UUID."""
        resp = await service_client.post(
            "/api/v1/internal/audit",
            json={
                "actor_id": "not-a-uuid",
                "action": "experiment.create",
                "scope_type": "project",
            },
        )
        assert resp.status == 400


class TestAuditLogQuery:
    """Tests for GET /api/v1/audit-log."""

    @pytest.mark.asyncio
    async def test_requires_auth(self, service_client):
        """GET /audit-log without token returns 401."""
        resp = await service_client.get("/api/v1/audit-log")
        assert resp.status == 401

    @pytest.mark.asyncio
    async def test_requires_audit_read_permission(self, service_client, regular_user_token):
        """Regular user without audit.read → 403."""
        resp = await service_client.get(
            "/api/v1/audit-log",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert resp.status == 403

    @pytest.mark.asyncio
    async def test_superadmin_can_read(self, service_client, superadmin_token):
        """Superadmin can always read audit log."""
        resp = await service_client.get(
            "/api/v1/audit-log",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert resp.status == 200
        data = await resp.json()
        assert isinstance(data, list)

    @pytest.mark.asyncio
    async def test_admin_with_audit_read_can_query(self, service_client, admin_user_token):
        """Admin (has audit.read) can query the log."""
        resp = await service_client.get(
            "/api/v1/audit-log",
            headers={"Authorization": f"Bearer {admin_user_token}"},
        )
        assert resp.status == 200

    @pytest.mark.asyncio
    async def test_entries_appear_after_ingest(self, service_client, superadmin_token):
        """After ingesting an entry, it should appear in the log."""
        actor_id = str(uuid4())
        unique_action = f"test.action.{uuid4().hex[:8]}"

        # Ingest entry
        ingest_resp = await service_client.post(
            "/api/v1/internal/audit",
            json={
                "actor_id": actor_id,
                "action": unique_action,
                "scope_type": "system",
            },
        )
        assert ingest_resp.status == 201

        # Query log
        resp = await service_client.get(
            f"/api/v1/audit-log?action={unique_action}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert resp.status == 200
        data = await resp.json()
        assert len(data) == 1
        assert data[0]["action"] == unique_action
        assert data[0]["actor_id"] == actor_id

    @pytest.mark.asyncio
    async def test_filter_by_actor_id(self, service_client, superadmin_token):
        """Filtering by actor_id returns only that actor's entries."""
        actor_id = str(uuid4())
        other_actor_id = str(uuid4())
        unique_action = f"test.actor.filter.{uuid4().hex[:8]}"

        await service_client.post(
            "/api/v1/internal/audit",
            json={"actor_id": actor_id, "action": unique_action, "scope_type": "system"},
        )
        await service_client.post(
            "/api/v1/internal/audit",
            json={"actor_id": other_actor_id, "action": unique_action, "scope_type": "system"},
        )

        resp = await service_client.get(
            f"/api/v1/audit-log?action={unique_action}&actor_id={actor_id}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert resp.status == 200
        data = await resp.json()
        assert all(e["actor_id"] == actor_id for e in data)

    @pytest.mark.asyncio
    async def test_pagination(self, service_client, superadmin_token):
        """limit and offset params control pagination."""
        unique_action = f"test.pagination.{uuid4().hex[:8]}"
        actor_id = str(uuid4())

        # Write 5 entries
        for _ in range(5):
            await service_client.post(
                "/api/v1/internal/audit",
                json={"actor_id": actor_id, "action": unique_action, "scope_type": "system"},
            )

        resp_all = await service_client.get(
            f"/api/v1/audit-log?action={unique_action}&limit=10",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        all_data = await resp_all.json()
        assert len(all_data) == 5

        resp_page = await service_client.get(
            f"/api/v1/audit-log?action={unique_action}&limit=2&offset=0",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        page_data = await resp_page.json()
        assert len(page_data) == 2

    @pytest.mark.asyncio
    async def test_response_structure(self, service_client, superadmin_token):
        """Returned entries have all required fields."""
        actor_id = str(uuid4())
        unique_action = f"test.structure.{uuid4().hex[:8]}"

        await service_client.post(
            "/api/v1/internal/audit",
            json={
                "actor_id": actor_id,
                "action": unique_action,
                "scope_type": "system",
                "target_type": "user",
                "target_id": str(uuid4()),
                "details": {"key": "value"},
            },
        )

        resp = await service_client.get(
            f"/api/v1/audit-log?action={unique_action}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert resp.status == 200
        entries = await resp.json()
        assert len(entries) == 1
        entry = entries[0]

        for field in ("id", "timestamp", "actor_id", "action", "scope_type", "details"):
            assert field in entry, f"Missing field: {field}"
        assert entry["target_type"] == "user"
        assert entry["details"] == {"key": "value"}

    @pytest.mark.asyncio
    async def test_login_creates_audit_entry(self, service_client, superadmin_token, database_url):
        """Logging in should create an auth.login audit entry."""
        import asyncpg

        # Create a fresh user for this test
        unique = uuid4().hex[:8]
        conn = await asyncpg.connect(str(database_url))
        try:
            result = await conn.fetchrow(
                "INSERT INTO users (username, email, hashed_password, password_change_required) "
                "VALUES ($1, $2, '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false) "
                "RETURNING id",
                f"audituser_{unique}",
                f"audituser_{unique}@example.com",
            )
            user_id = str(result["id"])
        finally:
            await conn.close()

        # Login
        login_resp = await service_client.post(
            "/auth/login",
            json={"username": f"audituser_{unique}", "password": "admin123"},
        )
        assert login_resp.status == 200

        # Verify audit entry was created
        resp = await service_client.get(
            f"/api/v1/audit-log?actor_id={user_id}&action=auth.login",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert resp.status == 200
        entries = await resp.json()
        assert len(entries) >= 1
        assert entries[0]["action"] == "auth.login"
        assert entries[0]["actor_id"] == user_id
