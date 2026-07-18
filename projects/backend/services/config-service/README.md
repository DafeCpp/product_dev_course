# config-service

Runtime configuration service for the platform. Provides feature flags, QoS parameters, and kill-switches.

Port: **8004**

## Quick start

```bash
make config-init    # create config_db + apply migrations
make dev-up         # start the full stack
curl http://localhost:8004/health
```

## API

- `POST /api/v1/config` — create config
- `GET /api/v1/config` — list configs
- `GET /api/v1/config/{id}` — get config
- `PATCH /api/v1/config/{id}` — update config (requires `If-Match`)
- `DELETE /api/v1/config/{id}` — soft delete
- `POST /api/v1/config/{id}/activate` — activate
- `POST /api/v1/config/{id}/deactivate` — deactivate
- `POST /api/v1/config/{id}/rollback` — rollback to previous version
- `GET /api/v1/config/{id}/history` — change history
- `GET /api/v1/configs/bulk` — bulk fetch for SDK polling (ETag/304 support)

## Consumer contracts

User-facing config endpoints are exposed through auth-proxy and redact sensitive
values unless the current user is a superadmin or has `configs.sensitive.read`.
This policy also applies to create, dry-run, history, lifecycle, rollback, and
idempotency replays.

`GET /api/v1/configs/bulk` is an internal service-to-service endpoint for SDK
polling and intentionally returns complete values, including sensitive ones. It
must only be reached over the internal network: auth-proxy returns `404` for the
route, and the development Docker publication is bound to `127.0.0.1`.

- `GET /api/v1/schemas` — list active JSON schemas
- `PUT /api/v1/schemas/{type}` — update schema (additive changes only)
