# Telemetry Ingest Service

Public REST ingest service for sensor telemetry.

## Endpoints

- `GET /health` — service health check
- `GET /openapi.yaml` — OpenAPI spec
- `POST /api/v1/telemetry` — ingest telemetry batch (`Authorization: Bearer <sensor_token>`)
- `GET /api/v1/telemetry/stream` — SSE stream of telemetry records for a sensor (Bearer token can be either sensor token or user JWT via auth-proxy; for SSE clients without headers, `access_token` query param is supported)

## Behavior (current semantics)

- **Sensors are NOT bound to runs/experiments**: a sensor just sends data for its project using its sensor token.
- **Capture session = recording window**:
  - When a project has an **active capture session** (`status in {running, backfilling}`), this service will **auto-attach** incoming telemetry to it.
  - If client omits `run_id` and `capture_session_id`, the service infers both from the project's active capture session.
  - If client provides `run_id`, the service will attach `capture_session_id` from the active session of that run (if any).
- **Stop means stop writing into the session**:
  - If client provides `capture_session_id` that is already finalized (`status in {succeeded, failed}`), the service will **NOT** attach new records to it (stores the record without `capture_session_id` and marks meta with `__system.late = true` and `__system.capture_session_attached = false`).

## Local development

Run via the root `docker-compose.yml` (recommended).

