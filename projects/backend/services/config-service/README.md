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
- `GET /api/v1/schemas` — list active JSON schemas
- `PUT /api/v1/schemas/{type}` — update schema (additive changes only)
