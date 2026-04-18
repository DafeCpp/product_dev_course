"""Prometheus business metrics for telemetry-ingest-service."""
from __future__ import annotations

from prometheus_client import Counter, Gauge

# ---------------------------------------------------------------------------
# Ingested readings
# ---------------------------------------------------------------------------

TELEMETRY_READINGS_INGESTED: Counter = Counter(
    "telemetry_readings_ingested_total",
    "Total number of telemetry readings successfully ingested",
    ["transport"],  # label values: 'rest', 'ws'
)

# ---------------------------------------------------------------------------
# Active long-lived connections
# ---------------------------------------------------------------------------

SSE_CONNECTIONS_ACTIVE: Gauge = Gauge(
    "sse_connections_active",
    "Number of currently active SSE (Server-Sent Events) connections",
)

WS_CONNECTIONS_ACTIVE: Gauge = Gauge(
    "ws_connections_active",
    "Number of currently active WebSocket connections",
)

# ---------------------------------------------------------------------------
# Rate limiting
# ---------------------------------------------------------------------------

INGEST_RATE_LIMITED: Counter = Counter(
    "ingest_rate_limited_total",
    "Total number of ingest requests rejected due to rate limiting",
    ["transport"],  # label values: 'rest', 'ws'
)
