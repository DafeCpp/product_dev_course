"""Per-user rate limit for heavy API endpoints, driven by ``QOS_CONFIG``.

``GET /api/v1/experiments`` and ``POST /api/v1/experiments/{id}/runs`` share
one budget of ``QOS_CONFIG.rate_limit_max_requests`` requests per user per
minute. The limit is read on every request, so an ``experiment_qos`` change
saved in config-service applies on the next poll without a restart.
"""
from __future__ import annotations

from experiment_service.middleware.export_rate_limit import ExportRateLimiter
from experiment_service.middleware.qos_config import QOS_CONFIG

API_RATE_LIMIT_WINDOW_SECONDS = 60.0

api_rate_limiter = ExportRateLimiter(
    max_requests=lambda: QOS_CONFIG.rate_limit_max_requests,
    window_seconds=API_RATE_LIMIT_WINDOW_SECONDS,
    message="Rate limit exceeded. Try again later.",
)
