# ADR-008: AsyncAPI Specification for SSE and WebSocket Channels

## Status

Accepted

## Date

2026-03-20

## Context

The Telemetry Ingest Service exposes two asynchronous channels that are not covered by the existing OpenAPI 3.1 specification:

1. **SSE (Server-Sent Events)** -- `GET /api/v1/telemetry/stream` -- real-time telemetry push to UI/consumers.
2. **WebSocket** -- `GET /api/v1/telemetry/ws` -- high-frequency sensor-to-server telemetry ingest.

OpenAPI is designed for request/response REST endpoints and cannot adequately describe:
- Bidirectional WebSocket message flows (client ingest message, server ack/error).
- SSE event types, heartbeat semantics, and cursor-based streaming.
- Per-channel rate limiting contracts and connection lifecycle.

The Experiment Service was also evaluated. Its `telemetry_export.py` uses `StreamResponse` for chunked HTTP file downloads (CSV/JSON), which is a standard HTTP response pattern, not an event-driven channel. No other SSE or WebSocket endpoints exist in experiment-service.

## Decision

We adopt **AsyncAPI 3.0** to document all event-driven channels. The specification lives alongside the service code:

- `projects/backend/services/telemetry-ingest-service/asyncapi.yaml`

The spec describes:
- **Servers** (development on port 8003, production with variable host).
- **Channels**: `telemetrySSE` and `telemetryWS` with full query parameter documentation.
- **Operations**: `streamTelemetry` (SSE send), `ingestTelemetryViaWS` (WS receive), `ackTelemetryViaWS` (WS send).
- **Message schemas**: exact JSON payloads matching the Pydantic DTOs (`TelemetryIngestDTO`, `WsIngestMessageDTO`) and the `_serialize_telemetry_record` output format.
- **Error codes**: all WebSocket error codes enumerated (`invalid_json`, `validation_error`, `rate_limited`, `unauthorized`, `bad_request`, `internal_error`, `unsupported`).

No AsyncAPI spec is created for experiment-service since it has no event-driven channels.

## Consequences

**Positive:**
- Frontend and firmware teams get a machine-readable contract for SSE/WS integration.
- AsyncAPI tooling can generate client stubs and documentation (e.g., AsyncAPI Studio, generators).
- Message schemas are documented alongside the service, reducing drift.

**Negative:**
- Another specification file to maintain; must be updated when SSE/WS contracts change.
- AsyncAPI 3.0 tooling ecosystem is less mature than OpenAPI.

**Mitigations:**
- CI can validate the AsyncAPI spec with `@asyncapi/cli validate`.
- Schema definitions in the spec mirror Pydantic DTOs; changes to DTOs should trigger spec updates (documented in contributing guide).
