from __future__ import annotations

import uuid

ROLE_PERMISSIONS: dict[str, str] = {
    "viewer": (
        "experiments.view,"
        "project.members.view"
    ),
    "editor": (
        "experiments.view,"
        "experiments.create,"
        "experiments.update,"
        "experiments.archive,"
        "runs.create,"
        "runs.update,"
        "metrics.write,"
        "project.members.view"
    ),
    "owner": (
        "experiments.view,"
        "experiments.create,"
        "experiments.update,"
        "experiments.delete,"
        "experiments.archive,"
        "runs.create,"
        "runs.update,"
        "metrics.write,"
        "project.settings.update,"
        "project.settings.delete,"
        "project.members.view,"
        "project.members.invite,"
        "project.members.remove,"
        "project.members.change_role,"
        "project.roles.manage"
    ),
}


def make_headers(
    project_id: uuid.UUID | None = None,
    role: str = "owner",
    *,
    user_id: uuid.UUID | None = None,
    superadmin: bool = False,
) -> dict[str, str]:
    """Construct RBAC v2 auth headers expected by the API gateway shim."""
    h: dict[str, str] = {
        "X-User-Id": str(user_id or uuid.uuid4()),
    }
    if project_id is not None:
        h["X-Project-Id"] = str(project_id)
    if superadmin:
        h["X-User-Is-Superadmin"] = "true"
        return h
    h["X-User-Is-Superadmin"] = "false"
    perms = ROLE_PERMISSIONS.get(role, "")
    # Support both tuple (legacy) and str so header is always comma-separated
    h["X-User-Permissions"] = "".join(perms) if isinstance(perms, tuple) else perms
    return h
