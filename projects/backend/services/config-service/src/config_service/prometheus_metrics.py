"""Domain metrics for config-service."""
from __future__ import annotations

from prometheus_client import Counter, Histogram

config_read_schema_violations_total = Counter(
    "config_read_schema_violations_total",
    "Number of schema violations detected on read (paranoid mode)",
    ["config_type", "config_id"],
)

config_compat_check_rejections_total = Counter(
    "config_compat_check_rejections_total",
    "Number of schema updates rejected by compat checker",
    ["config_type", "rule"],
)

config_sanity_check_failures_total = Counter(
    "config_sanity_check_failures_total",
    "Number of sanity check failures when updating schema",
    ["config_type"],
)

config_optimistic_lock_conflicts_total = Counter(
    "config_optimistic_lock_conflicts_total",
    "Number of 412 Precondition Failed responses due to version conflict",
    ["route"],
)

config_idempotency_hits_total = Counter(
    "config_idempotency_hits_total",
    "Idempotency key cache hits",
    ["result"],  # hit | conflict
)

config_bulk_responses_total = Counter(
    "config_bulk_responses_total",
    "Responses from bulk endpoint",
    ["status"],  # 200 | 304
)

config_propagation_lag_seconds = Histogram(
    "config_propagation_lag_seconds",
    "Time between config update and SDK poll (from updated_at)",
    buckets=[0.1, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0],
)
