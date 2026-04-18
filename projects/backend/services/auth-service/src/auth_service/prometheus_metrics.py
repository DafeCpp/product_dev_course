"""Prometheus business metrics for auth-service."""
from __future__ import annotations

from prometheus_client import Counter

# ---------------------------------------------------------------------------
# Login
# ---------------------------------------------------------------------------

AUTH_LOGINS: Counter = Counter(
    "auth_logins_total",
    "Total login attempts",
    ["result"],  # label values: 'success', 'failure'
)

# ---------------------------------------------------------------------------
# Token refresh
# ---------------------------------------------------------------------------

AUTH_TOKEN_REFRESHES: Counter = Counter(
    "auth_token_refreshes_total",
    "Total token refresh operations",
)

# ---------------------------------------------------------------------------
# Token reuse detection (security events)
# ---------------------------------------------------------------------------

AUTH_REUSE_DETECTIONS: Counter = Counter(
    "auth_reuse_detections_total",
    "Total token reuse detections (possible token theft)",
)
