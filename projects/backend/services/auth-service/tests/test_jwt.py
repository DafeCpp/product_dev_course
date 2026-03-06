"""Unit tests for auth_service.services.jwt module."""
from __future__ import annotations

import time
from unittest.mock import patch

import jwt
import pytest

from auth_service.services.jwt import (
    create_access_token,
    create_refresh_token,
    decode_token,
    get_jti_from_token,
    get_user_id_from_token,
)
from auth_service.settings import settings


class TestCreateAccessToken:
    """Tests for create_access_token function."""

    def test_creates_valid_token(self):
        """Test create_access_token creates a valid JWT."""
        user_id = "test-user-123"
        token = create_access_token(user_id)

        assert token is not None
        assert isinstance(token, str)
        assert len(token) > 0

    def test_token_contains_correct_user_id(self):
        """Test token payload contains correct user_id."""
        user_id = "user-456"
        token = create_access_token(user_id)

        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert decoded["sub"] == user_id

    def test_token_type_is_access(self):
        """Test token type claim is 'access'."""
        token = create_access_token("user-id")
        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert decoded["type"] == "access"

    def test_token_has_iat_claim(self):
        """Test token has issued-at (iat) claim."""
        before = int(time.time())
        token = create_access_token("user-id")
        after = int(time.time())

        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert "iat" in decoded
        assert before <= decoded["iat"] <= after

    def test_token_has_exp_claim(self):
        """Test token has expiration (exp) claim."""
        token = create_access_token("user-id")

        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert "exp" in decoded

    def test_token_expiration_is_correct(self):
        """Test token expiration is set correctly."""
        now = int(time.time())

        with patch("auth_service.services.jwt.time.time", return_value=now):
            token = create_access_token("user-id")

        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        expected_exp = now + settings.access_token_ttl_sec
        assert decoded["exp"] == expected_exp

    def test_different_users_get_different_tokens(self):
        """Test different users get different tokens."""
        token1 = create_access_token("user-1")
        token2 = create_access_token("user-2")

        assert token1 != token2

    def test_same_user_gets_different_tokens(self):
        """Test same user gets different tokens (due to time-based exp)."""
        token1 = create_access_token("user-1")
        # Delay to ensure different exp
        time.sleep(1.1)
        token2 = create_access_token("user-1")

        # Tokens should be different (different iat/exp)
        assert token1 != token2


class TestCreateRefreshToken:
    """Tests for create_refresh_token function."""

    def test_creates_valid_token(self):
        """Test create_refresh_token creates a valid JWT."""
        user_id = "test-user-123"
        token = create_refresh_token(user_id)

        assert token is not None
        assert isinstance(token, str)
        assert len(token) > 0

    def test_token_contains_correct_user_id(self):
        """Test token payload contains correct user_id."""
        user_id = "user-456"
        token = create_refresh_token(user_id)

        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert decoded["sub"] == user_id

    def test_token_type_is_refresh(self):
        """Test token type claim is 'refresh'."""
        token = create_refresh_token("user-id")
        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert decoded["type"] == "refresh"

    def test_token_has_jti_claim(self):
        """Test token has unique ID (jti) claim."""
        token1 = create_refresh_token("user-id")
        token2 = create_refresh_token("user-id")

        decoded1 = jwt.decode(
            token1,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        decoded2 = jwt.decode(
            token2,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )

        assert "jti" in decoded1
        assert "jti" in decoded2
        # JTI should be unique for each token
        assert decoded1["jti"] != decoded2["jti"]

    def test_token_has_iat_claim(self):
        """Test token has issued-at (iat) claim."""
        before = int(time.time())
        token = create_refresh_token("user-id")
        after = int(time.time())

        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert "iat" in decoded
        assert before <= decoded["iat"] <= after

    def test_token_has_exp_claim(self):
        """Test token has expiration (exp) claim."""
        token = create_refresh_token("user-id")

        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        assert "exp" in decoded

    def test_token_expiration_is_correct(self):
        """Test token expiration is set correctly."""
        now = int(time.time())

        with patch("auth_service.services.jwt.time.time", return_value=now):
            token = create_refresh_token("user-id")

        decoded = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        expected_exp = now + settings.refresh_token_ttl_sec
        assert decoded["exp"] == expected_exp

    def test_refresh_token_longer_lived_than_access(self):
        """Test refresh token lives longer than access token."""
        access_token = create_access_token("user-id")
        refresh_token = create_refresh_token("user-id")

        decoded_access = jwt.decode(
            access_token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        decoded_refresh = jwt.decode(
            refresh_token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )

        assert decoded_refresh["exp"] > decoded_access["exp"]


class TestDecodeToken:
    """Tests for decode_token function."""

    def test_decodes_valid_access_token(self):
        """Test decode_token decodes valid access token."""
        user_id = "test-user"
        token = create_access_token(user_id)

        payload = decode_token(token)

        assert payload["sub"] == user_id
        assert payload["type"] == "access"

    def test_decodes_valid_refresh_token(self):
        """Test decode_token decodes valid refresh token."""
        user_id = "test-user"
        token = create_refresh_token(user_id)

        payload = decode_token(token)

        assert payload["sub"] == user_id
        assert payload["type"] == "refresh"
        assert "jti" in payload

    def test_returns_dict(self):
        """Test decode_token returns a dict."""
        token = create_access_token("user-id")
        payload = decode_token(token)
        assert isinstance(payload, dict)

    def test_raises_on_expired_token(self):
        """Test decode_token raises ValueError for expired token."""
        now = int(time.time())

        # Create token that's already expired
        with patch("auth_service.services.jwt.time.time", return_value=now - 1000):
            token = create_access_token("user-id")

        with patch("auth_service.services.jwt.time.time", return_value=now):
            with pytest.raises(ValueError, match="Token expired"):
                decode_token(token)

    def test_raises_on_invalid_signature(self):
        """Test decode_token raises ValueError for invalid signature."""
        token = create_access_token("user-id")

        with patch("auth_service.services.jwt.settings.jwt_secret", "wrong-secret"):
            with pytest.raises(ValueError, match="Invalid token"):
                decode_token(token)

    def test_raises_on_malformed_token(self):
        """Test decode_token raises ValueError for malformed token."""
        with pytest.raises(ValueError, match="Invalid token"):
            decode_token("not.a.valid.token")

    def test_raises_on_empty_token(self):
        """Test decode_token raises ValueError for empty token."""
        with pytest.raises(ValueError, match="Invalid token"):
            decode_token("")

    def test_raises_on_none_token(self):
        """Test decode_token raises ValueError for None token."""
        with pytest.raises((ValueError, TypeError)):
            decode_token(None)

    def test_raises_on_tampered_token(self):
        """Test decode_token raises ValueError for tampered token."""
        user_id = "test-user"
        token = create_access_token(user_id)

        # Tamper with the payload
        parts = token.split(".")
        tampered_payload = parts[1] + "tampered"
        tampered_token = f"{parts[0]}.{tampered_payload}.{parts[2]}"

        with pytest.raises(ValueError, match="Invalid token"):
            decode_token(tampered_token)


class TestGetUserIdFromToken:
    """Tests for get_user_id_from_token function."""

    def test_extracts_user_id_from_access_token(self):
        """Test get_user_id_from_token extracts user_id from access token."""
        user_id = "test-user-123"
        token = create_access_token(user_id)

        extracted = get_user_id_from_token(token)

        assert extracted == user_id

    def test_extracts_user_id_from_refresh_token(self):
        """Test get_user_id_from_token extracts user_id from refresh token."""
        user_id = "test-user-456"
        token = create_refresh_token(user_id)

        extracted = get_user_id_from_token(token)

        assert extracted == user_id

    def test_raises_on_expired_token(self):
        """Test get_user_id_from_token raises on expired token."""
        now = int(time.time())

        with patch("auth_service.services.jwt.time.time", return_value=now - 1000):
            token = create_access_token("user-id")

        with patch("auth_service.services.jwt.time.time", return_value=now):
            with pytest.raises(ValueError, match="Token expired"):
                get_user_id_from_token(token)

    def test_raises_on_invalid_token(self):
        """Test get_user_id_from_token raises on invalid token."""
        with pytest.raises(ValueError, match="Invalid token"):
            get_user_id_from_token("invalid.token.here")

    def test_raises_when_sub_missing(self):
        """Test get_user_id_from_token raises when sub claim is missing."""
        # Create a token without sub claim
        payload = {
            "type": "access",
            "iat": int(time.time()),
            "exp": int(time.time()) + 3600,
        }
        token = jwt.encode(
            payload,
            settings.jwt_secret,
            algorithm=settings.jwt_algorithm,
        )

        with pytest.raises(ValueError, match="Token missing user ID"):
            get_user_id_from_token(token)


class TestGetJtiFromToken:
    """Tests for get_jti_from_token function."""

    def test_extracts_jti_from_refresh_token(self):
        """Test get_jti_from_token extracts jti from refresh token."""
        token = create_refresh_token("user-id")

        jti = get_jti_from_token(token)

        assert jti is not None
        assert isinstance(jti, str)
        assert len(jti) > 0

    def test_jti_is_valid_uuid_format(self):
        """Test JTI is in UUID format."""
        from uuid import UUID

        token = create_refresh_token("user-id")
        jti = get_jti_from_token(token)

        # Should not raise
        UUID(jti)

    def test_different_tokens_have_different_jti(self):
        """Test different refresh tokens have different JTI."""
        token1 = create_refresh_token("user-id")
        token2 = create_refresh_token("user-id")

        jti1 = get_jti_from_token(token1)
        jti2 = get_jti_from_token(token2)

        assert jti1 != jti2

    def test_raises_on_access_token(self):
        """Test get_jti_from_token raises on access token (no jti)."""
        token = create_access_token("user-id")

        with pytest.raises(ValueError, match="Token missing jti"):
            get_jti_from_token(token)

    def test_raises_on_expired_token(self):
        """Test get_jti_from_token raises on expired token."""
        # Create a token with very short expiration
        with patch("auth_service.services.jwt.settings.refresh_token_ttl_sec", 1):
            token = create_refresh_token("user-id")
        
        # Wait for token to expire
        time.sleep(1.5)
        
        # Should raise expired error
        with pytest.raises(ValueError, match="Token expired"):
            get_jti_from_token(token)

    def test_raises_on_invalid_token(self):
        """Test get_jti_from_token raises on invalid token."""
        with pytest.raises(ValueError, match="Invalid token"):
            get_jti_from_token("invalid.token.here")


class TestTokenIntegration:
    """Integration tests for JWT token flow."""

    def test_full_token_lifecycle(self):
        """Test complete token creation, decoding, and extraction."""
        user_id = "test-user"

        # Create tokens
        access_token = create_access_token(user_id)
        refresh_token = create_refresh_token(user_id)

        # Decode and verify
        access_payload = decode_token(access_token)
        refresh_payload = decode_token(refresh_token)

        assert access_payload["sub"] == user_id
        assert access_payload["type"] == "access"
        assert refresh_payload["sub"] == user_id
        assert refresh_payload["type"] == "refresh"

        # Extract user ID
        assert get_user_id_from_token(access_token) == user_id
        assert get_user_id_from_token(refresh_token) == user_id

        # Extract JTI from refresh token
        jti = get_jti_from_token(refresh_token)
        assert jti == refresh_payload["jti"]

    def test_token_verification_with_wrong_secret(self):
        """Test token created with one secret can't be verified with another."""
        user_id = "test-user"

        with patch("auth_service.services.jwt.settings.jwt_secret", "secret-1"):
            token = create_access_token(user_id)

        with patch("auth_service.services.jwt.settings.jwt_secret", "secret-2"):
            with pytest.raises(ValueError, match="Invalid token"):
                decode_token(token)

    def test_concurrent_token_creation(self):
        """Test creating multiple tokens concurrently."""
        import concurrent.futures

        def create_token(user_id: str) -> str:
            return create_access_token(user_id)

        user_ids = [f"user-{i}" for i in range(10)]

        with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
            tokens = list(executor.map(create_token, user_ids))

        # All tokens should be valid and unique
        assert len(tokens) == 10
        assert len(set(tokens)) == 10

        # Verify all tokens
        for user_id, token in zip(user_ids, tokens):
            payload = decode_token(token)
            assert payload["sub"] == user_id
            assert get_user_id_from_token(token) == user_id
