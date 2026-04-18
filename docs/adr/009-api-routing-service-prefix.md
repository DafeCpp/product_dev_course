# ADR 009: Явные имена сервисов в URL-маршрутах auth-proxy

Статус: proposed
Дата: 2026-03-20

## Контекст

Сейчас auth-proxy маршрутизирует запросы по следующей схеме:

| URL-паттерн | Целевой сервис |
|---|---|
| `ALL /api/*` | Experiment Service |
| `ALL /api/v1/users/*` | Auth Service (добавлено как патч поверх общего `/api/*`) |
| `ALL /projects/*` | Auth Service |
| `ALL /auth/*` | Auth Service |
| `ALL /api/v1/telemetry/*` | Telemetry Ingest Service |

**Проблемы текущей схемы:**

1. **Неявность маршрутизации** — глядя на URL `/api/v1/users/search`, невозможно понять куда уйдёт запрос без знания внутреннего устройства прокси.
2. **Хрупкость** — маршруты `auth-service` перекрывают `experiment-service` через более специфичный prefix и зависят от порядка регистрации плагинов в Fastify.
3. **Коллизии** — если в experiment-service появится `GET /api/v1/users/...`, он будет недостижим из-за конфликта с auth-service prefix.
4. **Сложность поддержки** — добавление нового сервиса требует аккуратного встраивания prefix в нужную позицию в `index.ts`.
5. **Debugging** — при 404 или неправильном ответе неочевидно, какой сервис его вернул.

## Решение

Ввести **явный префикс имени сервиса** в путях API:

```
/api/{service-name}/v{N}/{resource}
```

Примеры:

| Было | Станет |
|---|---|
| `GET /api/v1/experiments` | `GET /api/experiment-service/v1/experiments` |
| `GET /api/v1/runs/:id` | `GET /api/experiment-service/v1/runs/:id` |
| `GET /api/v1/users/search` | `GET /api/auth-service/v1/users/search` |
| `GET /api/v1/telemetry/stream` | `GET /api/telemetry-service/v1/telemetry/stream` |
| `GET /api/v1/sensors` | `GET /api/experiment-service/v1/sensors` |

Auth-proxy регистрирует три чистых, не перекрывающихся prefix:

```
/api/experiment-service  →  experiment-service:8002  (rewritePrefix: /api)
/api/auth-service        →  auth-service:8001        (rewritePrefix: /api)
/api/telemetry-service   →  telemetry-ingest:8003    (rewritePrefix: /api)
```

## Последствия

### Плюсы
- **Явность** — URL самодокументируют, какой сервис обрабатывает запрос.
- **Надёжность** — маршруты не зависят от порядка регистрации и не конфликтуют.
- **Масштабируемость** — добавление нового сервиса — это один `app.register()` с новым prefix.
- **Отладка** — логи и сетевой трафик сразу показывают целевой сервис.

### Минусы / Риски
- **Breaking change** — все вызовы в `frontend/src/api/client.ts` и других клиентах нужно обновить.
- **Миграция** — нужно обновить OpenAPI-спецификацию и сгенерированные SDK.
- **Временное состояние** — в период миграции нужно держать оба паттерна (обратная совместимость) или сделать единовременный переход.

## Объём изменений

### Auth Proxy (`index.ts`)
Заменить патч-регистрацию `/api/v1/users` на три явных prefix:

```typescript
// Было:
await app.register(httpProxy, { prefix: '/api/v1/users', upstream: authUrl, rewritePrefix: '/api/v1/users' })
await app.register(httpProxy, { prefix: '/api/v1/telemetry', upstream: telemetryUrl, rewritePrefix: '/api/v1/telemetry' })
await app.register(httpProxy, { prefix: '/api', upstream: experimentUrl, rewritePrefix: '/api' })

// Станет:
await app.register(httpProxy, { prefix: '/api/auth-service', upstream: authUrl, rewritePrefix: '/api' })
await app.register(httpProxy, { prefix: '/api/telemetry-service', upstream: telemetryUrl, rewritePrefix: '/api' })
await app.register(httpProxy, { prefix: '/api/experiment-service', upstream: experimentUrl, rewritePrefix: '/api' })
```

### Frontend (`src/api/client.ts`)
Обновить базовые пути запросов:

```typescript
// Было:
apiClient.get('/api/v1/experiments')
apiClient.get('/api/v1/users/search')
apiClient.get('/api/v1/telemetry/stream')

// Станет:
apiClient.get('/api/experiment-service/v1/experiments')
apiClient.get('/api/auth-service/v1/users/search')
apiClient.get('/api/telemetry-service/v1/telemetry/stream')
```

### OpenAPI спецификация
Обновить `servers[].url` и все `paths` в `experiment-service/openapi/openapi.yaml`.

### Telemetry CLI (`etp_cli.py`)
Обновить базовый URL если он использует прямые вызовы через прокси.

## Стратегия миграции

**Вариант A — единовременный переход (рекомендуется):**
1. Обновить auth-proxy (один коммит).
2. Обновить frontend (один коммит).
3. Обновить OpenAPI + регенерировать SDK.
4. Обновить Telemetry CLI.
5. Деплой всего стека одновременно.

**Вариант B — обратная совместимость:**
1. Auth-proxy регистрирует оба паттерна (`/api/v1/*` и `/api/{service}/v1/*`) параллельно.
2. Постепенно переводить клиентов на новые пути.
3. Удалить старые роуты после полной миграции.

## Открытые вопросы

- Стоит ли сохранять `/api/v1/*` как алиасы с редиректами на переходный период?
- Нужно ли менять маршруты `auth-service` (`/auth/*`, `/projects/*`) по той же логике на `/api/auth-service/v1/auth/*`?
- Как обрабатывать WebSocket upgrade при новой схеме маршрутизации?
