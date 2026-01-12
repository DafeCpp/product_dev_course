# Telemetry Ingest Service

Public REST ingest service for sensor telemetry.

## Endpoints

- `GET /health` — service health check
- `GET /openapi.yaml` — OpenAPI spec
- `POST /api/v1/telemetry` — ingest telemetry batch (`Authorization: Bearer <sensor_token>`)

## Local development

Run via the root `docker-compose.yml` (recommended).

