# Auth Proxy (Fastify)

Лёгкий BFF-прокси между фронтом и backend сервисами.

## Возможности (MVP)
- `/auth/*` проксируется в Auth Service.
- `/api/*` (REST + WS/SSE) проксируется в Experiment Service (далее — API Gateway).
- Куки access/refresh — HttpOnly, Secure (по конфигу), SameSite=Lax.
- CORS whitelist + credentials, базовый rate limit, healthcheck `/health`.
- Редактирование логов: Authorization/Cookie/Set-Cookie замаскированы.
- Проксируемый `/api/*` автоматически подставляет `Authorization: Bearer <access_cookie>` если кука есть.

## Запуск локально
```bash
cd frontend/apps/auth-proxy
npm install
cp env.example .env
npm run dev
```

## Переменные окружения
- `PORT` — порт прокси (по умолчанию 8080)
- `TARGET_EXPERIMENT_URL` — upstream Experiment Service (например, http://localhost:8002)
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

## CSRF (следующий шаг)
- Генерация токена (cookie + header `X-CSRF-Token`) при логине/refresh.
- Проверка на state-changing методах (POST/PUT/PATCH/DELETE) в прокси.
- Отключение для idempotent методов/health.

