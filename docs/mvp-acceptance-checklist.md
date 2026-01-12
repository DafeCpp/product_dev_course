# MVP acceptance checklist (sprints 1–3)

## 0. Start
- `cp env.docker.example .env`
- `cp docker-compose.override.yml.example docker-compose.override.yml` (optional, for dev)
- `docker-compose up -d`

Expected services (ports):
- Auth Service: `8001`
- Experiment Service: `8002`
- Telemetry Ingest Service: `8003`
- Auth Proxy: `8080`
- Experiment Portal: `3000` (dev) / `80` (prod)

## 1. Auth
- `POST http://localhost:8001/auth/register`
- `POST http://localhost:8001/auth/login`
- (Optional) open Portal and login through Auth Proxy

## 2. Create sensor (Experiment Service)
- `POST http://localhost:8002/api/v1/sensors` with project headers (or real auth)
- Save returned `sensor.id` and one-time `token`

## 3. Create experiment → run → capture session (Experiment Service)
- `POST /api/v1/experiments` → `experiment_id`
- `POST /api/v1/experiments/{experiment_id}/runs` → `run_id`
- `POST /api/v1/runs/{run_id}/capture-sessions` → `capture_session_id`

## 4. Ingest telemetry (Telemetry Ingest Service)
- `POST http://localhost:8003/api/v1/telemetry`
  - `Authorization: Bearer <sensor_token>`
  - Body includes `sensor_id`, optional `run_id`/`capture_session_id`, and non-empty `readings[]`

Expected:
- response is `202` with `{ "status": "accepted", "accepted": <n> }`
- `sensors.last_heartbeat` is updated
- `telemetry_records` has new rows

