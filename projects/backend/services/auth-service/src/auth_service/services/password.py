"""Password hashing utilities."""
from __future__ import annotations

import logging

import bcrypt

from auth_service.settings import settings

logger = logging.getLogger(__name__)


def hash_password(password: str) -> str:
    """Hash a password using bcrypt."""
    salt = bcrypt.gensalt(rounds=settings.bcrypt_rounds)
    hashed = bcrypt.hashpw(password.encode("utf-8"), salt)
    return hashed.decode("utf-8")


def verify_password(password: str, hashed_password: str) -> bool:
    """Verify a password against a hash.

    Returns False for invalid hashes or encoding errors.  Only ``bcrypt``
    specific exceptions (ValueError from malformed hash) are expected;
    anything else is logged as a warning so it does not go unnoticed.
    """
    try:
        return bcrypt.checkpw(
            password.encode("utf-8"),
            hashed_password.encode("utf-8"),
        )
    except (ValueError, TypeError):
        # Malformed hash or encoding issue - expected failure modes.
        return False
    except Exception:
        logger.warning("Unexpected error during password verification", exc_info=True)
        return False

