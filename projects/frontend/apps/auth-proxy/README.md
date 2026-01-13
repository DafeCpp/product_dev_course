# Auth Proxy (Fastify)

Лёгкий BFF-прокси между фронтом и backend сервисами.

## Возможности (MVP)
- `/auth/*` проксируется в Auth Service.
- `/api/v1/telemetry/*` (REST ingest + SSE stream) проксируется в Telemetry Ingest Service (с сохранением `Authorization: Bearer <sensor_token>` от клиента).
- `/api/*` (REST + WS/SSE) проксируется в Experiment Service (далее — API Gateway).
- Куки access/refresh — HttpOnly, Secure (по конфигу), SameSite=Lax.
- CORS whitelist + credentials, базовый rate limit, healthcheck `/health`.
- Редактирование логов: Authorization/Cookie/Set-Cookie замаскированы.
- Проксируемый `/api/*` автоматически подставляет `Authorization: Bearer <access_cookie>` если кука есть.

## Запуск локально
```bash
cd projects/frontend/apps/auth-proxy
npm install
cp env.example .env
npm run dev
```

## Переменные окружения
- `PORT` — порт прокси (по умолчанию 8080)
- `TARGET_EXPERIMENT_URL` — upstream Experiment Service (например, http://localhost:8002)
- `TARGET_TELEMETRY_URL` — upstream Telemetry Ingest Service (например, http://localhost:8003)
- `AUTH_URL` — upstream Auth Service (например, http://localhost:8001)
- `COOKIE_DOMAIN`, `COOKIE_SECURE`, `COOKIE_SAMESITE` — параметры установки куков
- `ACCESS_COOKIE_NAME`, `REFRESH_COOKIE_NAME` — названия куков
- `ACCESS_TTL_SEC`, `REFRESH_TTL_SEC` — TTL куков (сек)
- `CORS_ORIGINS` — список origin через запятую
- `RATE_LIMIT_WINDOW_MS`, `RATE_LIMIT_MAX` — настройки rate limit
- `LOG_LEVEL` — уровень логов Fastify

## Docker
```bash
docker build -t auth-proxy:dev .
docker run -p 8080:8080 --env-file env.example auth-proxy:dev
```

## CSRF

Реализовано как **double-submit cookie**:
- Auth Proxy выставляет cookie `csrf_token` (НЕ HttpOnly) при `POST /auth/login` и `POST /auth/refresh`.
- Для state-changing методов **POST/PUT/PATCH/DELETE** при наличии session cookies требуется заголовок `X-CSRF-Token`,
  равный значению cookie `csrf_token`.
- Исключения:
  - `/auth/login`, `/auth/refresh` (CSRF cookie ещё нет)
  - `/api/v1/telemetry/*` (там аутентификация по `Authorization: Bearer <sensor_token>`, а не по cookies)

