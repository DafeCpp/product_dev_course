"""Test helpers."""
from __future__ import annotations


def make_headers(
    user_id: str = "test-user",
    is_superadmin: bool = False,
    system_permissions: list[str] | None = None,
    project_permissions: dict[str, list[str]] | None = None,
) -> dict[str, str]:
    headers = {"X-User-Id": user_id}
    if is_superadmin:
        headers["X-User-Is-Superadmin"] = "true"
    if system_permissions:
        headers["X-User-System-Permissions"] = ",".join(system_permissions)
    if project_permissions:
        parts = [
            f"{proj}:{','.join(perms)}"
            for proj, perms in project_permissions.items()
        ]
        headers["X-User-Permissions"] = ";".join(parts)
    return headers


ADMIN_HEADERS = make_headers(
    user_id="admin-user",
    is_superadmin=True,
)

VIEWER_HEADERS = make_headers(
    user_id="viewer-user",
    system_permissions=["configs.view"],
)

EDITOR_HEADERS = make_headers(
    user_id="editor-user",
    system_permissions=[
        "configs.view",
        "configs.create",
        "configs.update",
        "configs.delete",
        "configs.activate",
        "configs.rollback",
    ],
)
