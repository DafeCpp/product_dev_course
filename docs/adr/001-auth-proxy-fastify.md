# ADR 001: Auth Proxy / BFF на Fastify

Статус: accepted
Дата: 2025-12-10

## Контекст
- Фронт не должен работать напрямую с сервисами: нужна сессионная аутентификация, нормализация ошибок, CORS/CSRF, audit.
- Нужна поддержка проксирования REST и WS/SSE (telemetry/live).
- Выбрали стек Fastify + TypeScript из-за производительности, готовых плагинов и типобезопасности.

## Решение
- Используем Fastify с плагинами: `@fastify/http-proxy`, `@fastify/cors`, `@fastify/rate-limit`, `@fastify/cookie`, `@fastify/formbody`, `@fastify/websocket` (по необходимости).
- Сессии: access/refresh в HttpOnly Secure SameSite=Lax куках. Ротация refresh на `/auth/refresh`.
- Проксирование: `/api/*` → Experiment Service (позже API Gateway); поддержка WS/SSE upgrade.
- Безопасность: CORS whitelist, CSRF (double-submit или header `X-CSRF-Token`), rate limit на логин и по сессии, маскирование секретов в логах, запрет логирования тела `/auth/*`.
- Наблюдаемость: request_id, базовые метрики (RPS/latency/error rate), логирование проксированных ошибок.
- Дополнено по реализации: `/api/*` автоматически подставляет `Authorization: Bearer <access_cookie>` и редактирует Authorization/Cookie/Set-Cookie в логах.

## Скоп MVP (минимальный API)
- `POST /auth/login` → Auth Service (обменивает credentials на access+refresh, ставит куки).
- `POST /auth/refresh` → Auth Service (ротация пары, обновляет куки).
- `POST /auth/logout` (чистит куки, опционально дергает revoke в Auth).
- `GET /auth/me` (валидирует access, возвращает профиль/роль).
- `ALL /api/*` (REST-прокси на Experiment Service, позже на API Gateway).
- `WS/SSE /api/*` (прокси upgrade/stream при живой телеметрии).

## Не цели MVP
- UI-страницы логина (делает фронт).
- Сторонние сервисы (Slack/Artifacts/etc.) — позже после интеграции с API Gateway.
- Тонкий RBAC внутри прокси — делаем только валидацию access токена.

## Переменные окружения
- `PORT` — порт прокси (по умолчанию 8080).
- `TARGET_EXPERIMENT_URL` — адрес Experiment Service (например, http://experiment-service:8002).
- `AUTH_URL` — адрес Auth Service для login/refresh/revoke.
- `COOKIE_DOMAIN`, `COOKIE_SECURE`, `SESSION_COOKIE_NAME`, `REFRESH_COOKIE_NAME`.
- `CORS_ORIGINS` — список разрешённых origin.
- `RATE_LIMIT_WINDOW_MS`, `RATE_LIMIT_MAX` — настройки rate limit.
- `CSRF_SECRET` — ключ для генерации/проверки CSRF-токена.
- `LOG_LEVEL` — уровень логирования.

## Docker Compose (набросок сервиса)
```yaml
  auth-proxy:
    build: ./frontend/apps/auth-proxy
    ports:
      - "8080:8080"
    environment:
      PORT: 8080
      TARGET_EXPERIMENT_URL: http://experiment-service:8002
      AUTH_URL: http://auth-service:8001
      COOKIE_DOMAIN: localhost
      COOKIE_SECURE: "false"
      CORS_ORIGINS: http://localhost:3000
      RATE_LIMIT_WINDOW_MS: 60000
      RATE_LIMIT_MAX: 60
      LOG_LEVEL: info
    depends_on:
      - experiment-service
```

## Безопасность
- Куки HttpOnly + Secure + SameSite=Lax, короткий TTL для access, ротация refresh.
- CSRF защита для state-changing запросов (header/дубль-токен).
- Маскирование Authorization/Set-Cookie в логах; запрет body-логов на `/auth/*`.
- Пробрасываем `X-Request-Id`; отбрасываем опасные заголовки при прокси.

## Наблюдаемость
- Логирование: метод, путь, статус, латентность, user_id (из access), request_id.
- Метрики: RPS, P95 latency, error rate, лимиты логина, доля 401/403.

## Открытые вопросы
- Где хранить реестр отозванных refresh (Auth Service или in-proxy cache)?
- Нужна ли поддержка mTLS между proxy и backend в dev/prod?
- Нужен ли fallback на API Gateway или прямой Experiment Service в первой версии?

