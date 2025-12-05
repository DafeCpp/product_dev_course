from __future__ import annotations

import uuid


def make_headers(
    project_id: uuid.UUID,
    role: str = "owner",
    *,
    user_id: uuid.UUID | None = None,
) -> dict[str, str]:
    """Construct auth headers expected by the API gateway shim."""
    return {
        "X-User-Id": str(user_id or uuid.uuid4()),
        "X-Project-Id": str(project_id),
        "X-Project-Role": role,
    }


