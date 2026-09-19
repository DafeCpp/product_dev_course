# P10-08 backend validation

Use only an isolated local test environment. The compose file contains public,
local-only test credentials and publishes API ports on 127.0.0.1. It does not
mount application data from an existing deployment. Select your Docker context
before running these commands from the repository root.

```sh
docker build -t p10-backend-base -f projects/backend/Dockerfile.base projects/backend
docker compose -p p10-validation -f docs/testing/p10-e2e.compose.json build
docker compose -p p10-validation -f docs/testing/p10-e2e.compose.json up -d --wait postgres redis
docker compose -p p10-validation -f docs/testing/p10-e2e.compose.json run --rm auth python -m bin.migrate
docker compose -p p10-validation -f docs/testing/p10-e2e.compose.json run --rm experiment python -m bin.migrate
docker compose -p p10-validation -f docs/testing/p10-e2e.compose.json up -d auth experiment proxy
```

Wait for `http://localhost:18001/health`, `http://localhost:18002/health` and
`http://localhost:18080/health` to return 200. Bootstrap the fresh test database:

```sh
curl --fail -sS -o /dev/null -H 'Content-Type: application/json' \
  -d '{"bootstrap_secret":"p10-local-bootstrap","username":"admin","email":"admin@example.com","password":"Admin123"}' \
  http://localhost:18001/auth/admin/bootstrap
npm ci
VITE_AUTH_PROXY_URL=http://localhost:18080 npm run dev --workspace experiment-tracking-frontend -- --host 127.0.0.1 --port 3005 --strictPort
```

In another terminal:

```sh
npx cypress run --project projects/frontend/apps/experiment-portal --spec 'projects/frontend/apps/experiment-portal/cypress/e2e/{experiment_lifecycle,login,shared_primitives}.cy.ts'
```

On systems with a standalone Compose binary, replace `docker compose` with
`docker-compose`. After testing, stop the frontend and remove this test stack:

```sh
docker compose -p p10-validation -f docs/testing/p10-e2e.compose.json down -v
```

## Results — 2026-09-19

Seven tests passed: lifecycle 2, login smoke 2, shared primitives 3. The lifecycle
uses real auth-service, experiment-service, auth-proxy, TimescaleDB and Redis;
no API response is stubbed. It creates a project, verifies persistence after
reload, opens its modal, selects it in experiments, creates an experiment,
returns through its list/detail route, creates a run, reloads run details,
opens telemetry and logs out. Project screenshots at 390/1440px have no
horizontal overflow. This verifies the telemetry page, not sensor ingestion.
The shared-primitives spec intentionally continues to stub loading/error states.

## Separate pre-existing failure

`system_roles.cy.ts` was also run and failed before creating a role:
`GET /api/v1/permissions` returns `{id, scope_type, category, description}`, but
`PermissionPicker` consumes `{id, name, scope, category, description}`. Filtering
by `scope` produces an empty picker. These files were not changed by P10-08;
this backend/frontend contract issue is outside shared primitive adoption.
Track it separately; the complete Cypress suite is not claimed to be green.

Manual VoiceOver validation was explicitly moved to task_tracker P10-10;
automated keyboard and accessible-role tests remain part of P10-08 evidence.
