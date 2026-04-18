"""Integration tests for ProjectService with RBAC v2 (uses real DB via testsuite)."""
import pytest
from uuid import uuid4

from auth_service.core.exceptions import ForbiddenError, NotFoundError
from auth_service.repositories.projects import ProjectRepository
from auth_service.repositories.users import UserRepository
from auth_service.repositories.user_roles import UserRoleRepository
from auth_service.repositories.permissions import PermissionRepository
from auth_service.repositories.roles import RoleRepository
from auth_service.services.permission import PermissionService
from auth_service.services.projects import ProjectService
import asyncpg


@pytest.fixture
async def project_service(pool_with_seed):
    """Create ProjectService with test database."""
    perm_svc = PermissionService(
        PermissionRepository(pool_with_seed),
        RoleRepository(pool_with_seed),
        UserRoleRepository(pool_with_seed),
    )
    return ProjectService(
        ProjectRepository(pool_with_seed),
        UserRepository(pool_with_seed),
        UserRoleRepository(pool_with_seed),
        perm_svc,
    )


@pytest.fixture
async def test_user(database_url):
    """Create a test user."""
    conn = await asyncpg.connect(database_url)
    try:
        result = await conn.fetchrow("""
            INSERT INTO users (username, email, hashed_password, password_change_required, is_active)
            VALUES ('testuser', 'test@example.com', 
                    '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', 
                    false, true)
            RETURNING id
        """)
        return result["id"]
    finally:
        await conn.close()


@pytest.fixture
async def superadmin_user(test_user, database_url):
    """Make test_user a superadmin."""
    conn = await asyncpg.connect(database_url)
    try:
        await conn.execute("""
            INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at)
            VALUES ($1, '00000000-0000-0000-0000-000000000001', $1, now())
        """, test_user)
        return test_user
    finally:
        await conn.close()


class TestCreateProject:
    """Tests for create_project method."""

    @pytest.mark.asyncio
    async def test_create_project_requires_permission(self, project_service, test_user, database_url):
        """Any authenticated user can create projects (no permission check)."""
        # NOTE: create_project does not require special permission - any authenticated user can create projects
        # The owner is automatically granted owner role via database trigger
        project = await project_service.create_project(
            name="Test Project",
            description="Test",
            owner_id=test_user,
        )
        assert project.name == "Test Project"
        assert project.owner_id == test_user

    @pytest.mark.asyncio
    async def test_create_project_success(self, project_service, superadmin_user, database_url):
        """Superadmin can create projects."""
        project = await project_service.create_project(
            name="Test Project",
            description="Test Description",
            owner_id=superadmin_user,
        )
        assert project.name == "Test Project"
        assert project.description == "Test Description"
        assert project.owner_id == superadmin_user


class TestGetProject:
    """Tests for get_project method."""

    @pytest.mark.asyncio
    async def test_get_project_requires_permission(self, project_service, test_user, grantor_user, database_url):
        """User without project.members.view cannot access project."""
        conn = await asyncpg.connect(database_url)
        try:
            # Create project with grantor_user as owner (test_user is not a member)
            result = await conn.fetchrow("""
                INSERT INTO projects (name, description, owner_id)
                VALUES ('Test Project', 'Test', $1)
                RETURNING id
            """, grantor_user)
            project_id = result["id"]
        finally:
            await conn.close()

        # test_user is not a member, so should not have project.members.view
        with pytest.raises(ForbiddenError, match="Missing permission"):
            await project_service.get_project(project_id, test_user)

    @pytest.mark.asyncio
    async def test_get_project_with_owner_permission(self, project_service, test_user, database_url):
        """Project owner can access their project."""
        import asyncpg
        conn = await asyncpg.connect(database_url)
        try:
            # Create project
            result = await conn.fetchrow("""
                INSERT INTO projects (name, description, owner_id)
                VALUES ('Test Project', 'Test', $1)
                RETURNING id
            """, test_user)
            project_id = result["id"]
            
            # Owner role is automatically assigned via trigger
        finally:
            await conn.close()

        project = await project_service.get_project(project_id, test_user)
        assert project.name == "Test Project"


class TestUpdateProject:
    """Tests for update_project method."""

    @pytest.mark.asyncio
    async def test_update_project_requires_permission(self, project_service, test_user, grantor_user, database_url):
        """User without project.settings.update cannot update project."""
        conn = await asyncpg.connect(database_url)
        try:
            # Create project with grantor_user as owner
            result = await conn.fetchrow("""
                INSERT INTO projects (name, description, owner_id)
                VALUES ('Test Project', 'Test', $1)
                RETURNING id
            """, grantor_user)
            project_id = result["id"]
        finally:
            await conn.close()

        # test_user is not a member, so should not have project.settings.update
        with pytest.raises(ForbiddenError, match="Missing permission"):
            await project_service.update_project(
                project_id=project_id,
                user_id=test_user,
                name="Updated Name",
            )


class TestDeleteProject:
    """Tests for delete_project method."""

    @pytest.mark.asyncio
    async def test_delete_project_requires_permission(self, project_service, test_user, grantor_user, database_url):
        """User without project.settings.delete cannot delete project."""
        conn = await asyncpg.connect(database_url)
        try:
            # Create project with grantor_user as owner
            result = await conn.fetchrow("""
                INSERT INTO projects (name, description, owner_id)
                VALUES ('Test Project', 'Test', $1)
                RETURNING id
            """, grantor_user)
            project_id = result["id"]
        finally:
            await conn.close()

        # test_user is not a member, so should not have project.settings.delete
        with pytest.raises(ForbiddenError, match="Missing permission"):
            await project_service.delete_project(project_id, test_user)


class TestListMembers:
    """Tests for list_members method."""

    @pytest.mark.asyncio
    async def test_list_members_requires_permission(self, project_service, test_user, grantor_user, database_url):
        """User without project.members.view cannot list members."""
        conn = await asyncpg.connect(database_url)
        try:
            # Create project with grantor_user as owner
            result = await conn.fetchrow("""
                INSERT INTO projects (name, description, owner_id)
                VALUES ('Test Project', 'Test', $1)
                RETURNING id
            """, grantor_user)
            project_id = result["id"]
        finally:
            await conn.close()

        # test_user is not a member, so should not have project.members.view
        with pytest.raises(ForbiddenError, match="Missing permission"):
            await project_service.list_members(project_id, test_user)


class TestAddMember:
    """Tests for add_member method."""

    @pytest.mark.asyncio
    async def test_add_member_requires_permission(self, project_service, test_user, grantor_user, database_url):
        """User without project.members.invite cannot add members."""
        conn = await asyncpg.connect(database_url)
        try:
            # Create project with grantor_user as owner
            result = await conn.fetchrow("""
                INSERT INTO projects (name, description, owner_id)
                VALUES ('Test Project', 'Test', $1)
                RETURNING id
            """, grantor_user)
            project_id = result["id"]

            # Create another user
            user2_result = await conn.fetchrow("""
                INSERT INTO users (username, email, hashed_password, password_change_required, is_active)
                VALUES ('user2', 'user2@example.com',
                        '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG',
                        false, true)
                RETURNING id
            """)
            user2_id = user2_result["id"]
        finally:
            await conn.close()

        # test_user is not a member, so should not have project.members.invite
        with pytest.raises(ForbiddenError, match="Missing permission"):
            await project_service.add_member(
                project_id=project_id,
                requester_id=test_user,
                new_user_id=user2_id,
                role_id=uuid4(),
            )


class TestRemoveMember:
    """Tests for remove_member method."""

    @pytest.mark.asyncio
    async def test_remove_member_requires_permission(self, project_service, test_user, grantor_user, database_url):
        """User without project.members.remove cannot remove members."""
        conn = await asyncpg.connect(database_url)
        try:
            # Create project with grantor_user as owner
            result = await conn.fetchrow("""
                INSERT INTO projects (name, description, owner_id)
                VALUES ('Test Project', 'Test', $1)
                RETURNING id
            """, grantor_user)
            project_id = result["id"]

            # Create another user
            user2_result = await conn.fetchrow("""
                INSERT INTO users (username, email, hashed_password, password_change_required, is_active)
                VALUES ('user2', 'user2@example.com',
                        '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG',
                        false, true)
                RETURNING id
            """)
            user2_id = user2_result["id"]
            
            # Add user2 as member
            await conn.execute("""
                INSERT INTO user_project_roles (user_id, project_id, role_id, granted_by, granted_at)
                VALUES ($1, $2, '00000000-0000-0000-0000-000000000012', $3, now())
            """, user2_id, project_id, test_user)
        finally:
            await conn.close()

        with pytest.raises(ForbiddenError, match="Missing permission"):
            await project_service.remove_member(
                project_id=project_id,
                requester_id=test_user,
                member_user_id=user2_id,
                role_id=uuid4(),
            )

    @pytest.mark.asyncio
    async def test_cannot_remove_owner(self, project_service, test_user, database_url):
        """Cannot remove project owner."""
        import asyncpg
        conn = await asyncpg.connect(database_url)
        try:
            # Create project
            result = await conn.fetchrow("""
                INSERT INTO projects (name, description, owner_id)
                VALUES ('Test Project', 'Test', $1)
                RETURNING id
            """, test_user)
            project_id = result["id"]
            
            # Make test_user have projects.remove permission (via admin role)
            await conn.execute("""
                INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at)
                VALUES ($1, '00000000-0000-0000-0000-000000000002', $1, now())
            """, test_user)
        finally:
            await conn.close()

        # Try to remove owner (test_user) - should fail
        with pytest.raises(ForbiddenError, match="Cannot remove project owner"):
            await project_service.remove_member(
                project_id=project_id,
                requester_id=test_user,
                member_user_id=test_user,
                role_id=uuid4(),
            )
