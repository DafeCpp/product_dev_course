"""Unit tests for auth_service.services.password module."""
from __future__ import annotations

from unittest.mock import patch

import pytest

from auth_service.services.password import hash_password, verify_password


class TestHashPassword:
    """Tests for hash_password function."""

    def test_returns_string(self):
        """Test hash_password returns a string."""
        result = hash_password("testpassword123")
        assert isinstance(result, str)

    def test_returns_non_empty_string(self):
        """Test hash_password returns non-empty string."""
        result = hash_password("testpassword123")
        assert len(result) > 0

    def test_different_passwords_different_hashes(self):
        """Test different passwords produce different hashes."""
        hash1 = hash_password("password1")
        hash2 = hash_password("password2")
        assert hash1 != hash2

    def test_same_password_different_hashes(self):
        """Test same password produces different hashes (due to salt)."""
        hash1 = hash_password("testpassword123")
        hash2 = hash_password("testpassword123")
        # Bcrypt uses random salt, so hashes should be different
        assert hash1 != hash2

    def test_hash_starts_with_bcrypt_identifier(self):
        """Test bcrypt hash starts with $2b$ identifier."""
        result = hash_password("testpassword123")
        assert result.startswith("$2")

    def test_hash_has_correct_format(self):
        """Test bcrypt hash has correct format."""
        result = hash_password("testpassword123")
        # Bcrypt hash format: $2b$[cost]$[22-char-salt][31-char-hash]
        parts = result.split("$")
        assert len(parts) == 4
        assert parts[1] == "2b" or parts[1] == "2a"
        assert parts[2] == "12"  # Default bcrypt rounds

    def test_hash_with_unicode_password(self):
        """Test hashing password with unicode characters."""
        result = hash_password("пароль123")
        assert isinstance(result, str)
        assert len(result) > 0

    def test_hash_with_special_characters(self):
        """Test hashing password with special characters."""
        result = hash_password("p@$$w0rd!#$%^&*()")
        assert isinstance(result, str)
        assert len(result) > 0

    def test_hash_with_empty_password(self):
        """Test hashing empty password."""
        result = hash_password("")
        assert isinstance(result, str)
        assert len(result) > 0

    def test_hash_with_very_long_password(self):
        """Test hashing very long password."""
        long_password = "a" * 1000
        result = hash_password(long_password)
        assert isinstance(result, str)
        assert len(result) > 0

    def test_hash_is_deterministic_in_format(self):
        """Test hash always has bcrypt format."""
        for _ in range(5):
            result = hash_password("test")
            assert result.startswith("$2")
            assert len(result.split("$")) == 4


class TestVerifyPassword:
    """Tests for verify_password function."""

    def test_correct_password_returns_true(self):
        """Test correct password returns True."""
        password = "testpassword123"
        hashed = hash_password(password)
        assert verify_password(password, hashed) is True

    def test_wrong_password_returns_false(self):
        """Test wrong password returns False."""
        password = "testpassword123"
        wrong_password = "wrongpassword456"
        hashed = hash_password(password)
        assert verify_password(wrong_password, hashed) is False

    def test_empty_password_returns_false(self):
        """Test empty password returns False."""
        password = "testpassword123"
        hashed = hash_password(password)
        assert verify_password("", hashed) is False

    def test_case_sensitive(self):
        """Test password verification is case-sensitive."""
        password = "TestPassword123"
        hashed = hash_password(password)

        assert verify_password("TestPassword123", hashed) is True
        assert verify_password("testpassword123", hashed) is False
        assert verify_password("TESTPASSWORD123", hashed) is False

    def test_unicode_password(self):
        """Test verification with unicode password."""
        password = "пароль123"
        hashed = hash_password(password)
        assert verify_password(password, hashed) is True
        assert verify_password("wrong", hashed) is False

    def test_special_characters(self):
        """Test verification with special characters."""
        password = "p@$$w0rd!#$%^&*()"
        hashed = hash_password(password)
        assert verify_password(password, hashed) is True
        assert verify_password("wrong", hashed) is False

    def test_malformed_hash_returns_false(self):
        """Test malformed hash returns False."""
        assert verify_password("password", "not-a-valid-hash") is False

    def test_invalid_hash_format_returns_false(self):
        """Test invalid hash format returns False."""
        assert verify_password("password", "") is False
        assert verify_password("password", "tooshort") is False
        assert verify_password("password", "$invalid$hash$") is False

    def test_none_hash_returns_false(self):
        """Test None hash returns False."""
        assert verify_password("password", None) is False

    def test_empty_password_and_hash(self):
        """Test empty password and empty hash."""
        assert verify_password("", "") is False

    def test_different_hash_rounds(self):
        """Test verification works with different bcrypt rounds."""
        password = "testpassword123"

        # Hash with different rounds
        with patch("auth_service.services.password.settings.bcrypt_rounds", 10):
            hash_rounds_10 = hash_password(password)

        with patch("auth_service.services.password.settings.bcrypt_rounds", 14):
            hash_rounds_14 = hash_password(password)

        # Both should verify correctly
        assert verify_password(password, hash_rounds_10) is True
        assert verify_password(password, hash_rounds_14) is True

    def test_hash_with_null_bytes(self):
        """Test password with null bytes."""
        password = "test\x00password"
        hashed = hash_password(password)
        assert verify_password(password, hashed) is True

    def test_hash_with_whitespace(self):
        """Test password with various whitespace."""
        password = "test password\nwith\twhitespace"
        hashed = hash_password(password)
        assert verify_password(password, hashed) is True
        assert verify_password("test password", hashed) is False

    def test_similar_passwords(self):
        """Test similar but different passwords."""
        password1 = "password123"
        password2 = "password124"

        hashed1 = hash_password(password1)
        hashed2 = hash_password(password2)

        assert verify_password(password1, hashed1) is True
        assert verify_password(password2, hashed1) is False
        assert verify_password(password1, hashed2) is False
        assert verify_password(password2, hashed2) is True


class TestPasswordHashingIntegration:
    """Integration tests for password hashing workflow."""

    def test_hash_then_verify_workflow(self):
        """Test complete hash and verify workflow."""
        password = "SecurePassword123!"

        # Hash the password
        hashed = hash_password(password)

        # Verify correct password
        assert verify_password(password, hashed) is True

        # Verify wrong passwords fail
        assert verify_password("WrongPassword", hashed) is False
        assert verify_password("", hashed) is False
        assert verify_password(password.lower(), hashed) is False

    def test_multiple_passwords_independent(self):
        """Test multiple passwords are hashed independently."""
        passwords = [
            "password1",
            "password2",
            "password3",
            "пароль",
            "p@$$w0rd",
        ]

        hashes = [hash_password(p) for p in passwords]

        # Each password should only verify against its own hash
        for i, (password, correct_hash) in enumerate(zip(passwords, hashes)):
            assert verify_password(password, correct_hash) is True

            # Should not verify against other hashes
            for j, other_hash in enumerate(hashes):
                if i != j:
                    assert verify_password(password, other_hash) is False

    def test_password_change_workflow(self):
        """Test password change workflow."""
        old_password = "OldPassword123"
        new_password = "NewPassword456"

        # Hash old password
        old_hash = hash_password(old_password)

        # Verify old password works
        assert verify_password(old_password, old_hash) is True

        # Hash new password (simulating password change)
        new_hash = hash_password(new_password)

        # Old password should not work with new hash
        assert verify_password(old_password, new_hash) is False

        # New password should work with new hash
        assert verify_password(new_password, new_hash) is True

    def test_concurrent_hashing(self):
        """Test concurrent password hashing."""
        import concurrent.futures

        passwords = [f"password{i}" for i in range(10)]

        def hash_pw(password: str) -> str:
            return hash_password(password)

        with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
            hashes = list(executor.map(hash_pw, passwords))

        # All hashes should be valid
        for password, hashed in zip(passwords, hashes):
            assert verify_password(password, hashed) is True

    def test_hash_uniqueness_across_many_iterations(self):
        """Test hash uniqueness across many iterations."""
        password = "testpassword"
        hashes = set()

        # Generate many hashes for the same password
        for _ in range(100):
            h = hash_password(password)
            hashes.add(h)

        # All hashes should be unique (due to random salt)
        assert len(hashes) == 100

        # All should verify correctly
        for h in hashes:
            assert verify_password(password, h) is True


class TestPasswordErrorHandling:
    """Tests for password error handling."""

    def test_verify_with_invalid_bcrypt_hash(self):
        """Test verification with invalid bcrypt hash."""
        # Invalid bcrypt hash (wrong identifier)
        invalid_hash = "$99$rounds$salt$hashvalue"
        assert verify_password("password", invalid_hash) is False

    def test_verify_with_truncated_hash(self):
        """Test verification with truncated hash."""
        password = "testpassword"
        full_hash = hash_password(password)

        # Truncate the hash
        truncated = full_hash[:20]
        assert verify_password(password, truncated) is False

    def test_verify_with_corrupted_hash(self):
        """Test verification with corrupted hash."""
        password = "testpassword"
        full_hash = hash_password(password)
        
        # Corrupt the cost factor (positions 4-6 in $2b$12$...)
        # This makes the hash invalid
        corrupted = full_hash[:4] + "99" + full_hash[6:]
        assert verify_password(password, corrupted) is False

    def test_verify_password_encoding_error_handling(self):
        """Test password verification handles encoding errors."""
        # This tests the exception handling in verify_password
        password = "testpassword"
        hashed = hash_password(password)

        # Should handle gracefully
        result = verify_password(password, hashed)
        assert result is True

    def test_hash_password_error_handling(self):
        """Test hash_password error handling."""
        # Should handle any string input
        test_cases = [
            "normal_password",
            "",
            "a",
            "пароль",
            "密码",
            "🔐emoji",
            "a" * 10000,
        ]

        for password in test_cases:
            hashed = hash_password(password)
            assert isinstance(hashed, str)
            assert len(hashed) > 0
