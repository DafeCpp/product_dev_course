"""Prometheus business metrics for experiment-service."""
from __future__ import annotations

from prometheus_client import Counter, Gauge

# ---------------------------------------------------------------------------
# Experiments
# ---------------------------------------------------------------------------

EXPERIMENTS_CREATED: Counter = Counter(
    "experiments_created_total",
    "Total number of experiments created",
)

# ---------------------------------------------------------------------------
# Runs
# ---------------------------------------------------------------------------

RUNS_CREATED: Counter = Counter(
    "runs_created_total",
    "Total number of runs created",
)

# ---------------------------------------------------------------------------
# Capture sessions
# ---------------------------------------------------------------------------

CAPTURE_SESSIONS_ACTIVE: Gauge = Gauge(
    "capture_sessions_active",
    "Number of currently active (running/backfilling) capture sessions",
)

# ---------------------------------------------------------------------------
# Webhooks
# ---------------------------------------------------------------------------

WEBHOOK_DELIVERIES: Counter = Counter(
    "webhook_deliveries_total",
    "Total webhook deliveries enqueued",
    ["event_type"],
)
