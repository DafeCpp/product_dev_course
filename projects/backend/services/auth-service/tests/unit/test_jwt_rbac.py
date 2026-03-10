"""Unit tests for JWT with RBAC v2 claims."""
import pytest
import jwt
from unittest.mock import patch
import time

from auth_service.services.jwt import (
    create_access_token,
    decode_token,
    get_claims_from_token,
)
from auth_service.settings import settings


class TestCreateAccessTokenWithRBAC:
    """Tests for create_access_token with RBAC v2 claims."""

    def test_create_token_without_claims(self):
        """Token without RBAC claims has no sa or sys."""
        token = create_access_token("user-123")
        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert "sa" not in decoded or decoded.get("sa") is False
        assert "sys" not in decoded

    def test_create_superadmin_token(self):
        """Superadmin token has sa=true."""
        token = create_access_token("user-123", is_superadmin=True)
        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert decoded["sa"] is True
        assert "sys" not in decoded  # superadmin doesn't need sys permissions

    def test_create_token_with_system_permissions(self):
        """Regular user token includes system permissions."""
        perms = ["users.list", "audit.read", "scripts.execute"]
        token = create_access_token("user-123", is_superadmin=False, system_permissions=perms)
        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert decoded.get("sa", False) is False
        assert decoded["sys"] == perms

    def test_create_token_with_empty_permissions(self):
        """Token with empty permissions has no sys claim."""
        token = create_access_token("user-123", is_superadmin=False, system_permissions=[])
        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert "sys" not in decoded

    def test_superadmin_flag_takes_precedence(self):
        """When is_superadmin=True, system_permissions are ignored."""
        perms = ["users.list"]
        token = create_access_token("user-123", is_superadmin=True, system_permissions=perms)
        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert decoded["sa"] is True
        assert "sys" not in decoded


class TestGetClaimsFromToken:
    """Tests for get_claims_from_token function."""

    def test_get_claims_from_regular_token(self):
        """Extract claims from regular user token."""
        perms = ["users.list", "audit.read"]
        token = create_access_token("user-123", is_superadmin=False, system_permissions=perms)
        
        claims = get_claims_from_token(token)
        
        assert claims["sub"] == "user-123"
        assert claims["sa"] is False
        assert claims["sys"] == perms

    def test_get_claims_from_superadmin_token(self):
        """Extract claims from superadmin token."""
        token = create_access_token("user-123", is_superadmin=True)
        
        claims = get_claims_from_token(token)
        
        assert claims["sub"] == "user-123"
        assert claims["sa"] is True
        assert claims["sys"] == []

    def test_get_claims_from_token_without_rbac(self):
        """Extract claims from token without RBAC claims (backward compatibility)."""
        # Create old-style token without RBAC claims
        now = int(time.time())
        old_payload = {
            "sub": "user-123",
            "type": "access",
            "iat": now,
            "exp": now + 3600,
        }
        token = jwt.encode(
            old_payload,
            settings.jwt_secret,
            algorithm=settings.jwt_algorithm,
        )
        
        claims = get_claims_from_token(token)
        
        assert claims["sub"] == "user-123"
        assert claims["sa"] is False  # default
        assert claims["sys"] == []  # default


class TestTokenRBACIntegration:
    """Integration tests for RBAC claims in tokens."""

    def test_token_flow_with_permissions(self):
        """Full token lifecycle with RBAC claims."""
        user_id = "test-user"
        perms = ["users.list", "users.create", "roles.assign"]
        
        # Create token
        token = create_access_token(user_id, is_superadmin=False, system_permissions=perms)
        
        # Decode and verify
        claims = get_claims_from_token(token)
        assert claims["sub"] == user_id
        assert claims["sa"] is False
        assert claims["sys"] == perms
        
        # Verify with raw decode
        decoded = decode_token(token)
        assert decoded["sys"] == perms

    def test_superadmin_token_flow(self):
        """Superadmin token flow."""
        user_id = "admin-user"
        
        token = create_access_token(user_id, is_superadmin=True)
        claims = get_claims_from_token(token)
        
        assert claims["sa"] is True
        assert claims["sys"] == []
        
        # Verify superadmin has all permissions implicitly
        decoded = decode_token(token)
        assert decoded["sa"] is True

    def test_multiple_users_different_permissions(self):
        """Different users get different permission sets."""
        user1_perms = ["users.list"]
        user2_perms = ["scripts.execute", "configs.read"]
        
        token1 = create_access_token("user-1", system_permissions=user1_perms)
        token2 = create_access_token("user-2", system_permissions=user2_perms)
        
        claims1 = get_claims_from_token(token1)
        claims2 = get_claims_from_token(token2)
        
        assert claims1["sys"] == user1_perms
        assert claims2["sys"] == user2_perms
        assert claims1["sys"] != claims2["sys"]
