# Инвентаризация сервисов для локального запуска

Документ описывает все сервисы и компоненты системы, необходимые для локального запуска через Docker.

## Дата создания
2025-12-XX

## Реализованные сервисы

### 1. Experiment Service (Backend)
- **Путь:** `projects/backend/services/experiment-service/`
- **Технологии:** Python 3.14, aiohttp 3.10, asyncpg 0.29, PostgreSQL
- **Порт:** 8002
- **Dockerfile:** ✅ Есть (`projects/backend/services/experiment-service/Dockerfile`)
- **docker-compose.yml:** ✅ Есть (локальный, только для experiment-service)
- **env.example:** ✅ Есть
- **Зависимости:**
  - PostgreSQL 16 (для хранения данных)
  - Auth Service (опционально, можно использовать заглушки через заголовки)
  - RabbitMQ (упоминается, но не используется активно)
  - Redis (для telemetry broker, упоминается)
- **Статус:** ✅ Полностью реализован и готов к запуску

### 2. Auth Proxy (Frontend BFF)
- **Путь:** `projects/frontend/apps/auth-proxy/`
- **Технологии:** Node.js 24 (LTS), TypeScript, Fastify
- **Порт:** 8080
- **Dockerfile:** ✅ Есть (`projects/frontend/apps/auth-proxy/Dockerfile`)
- **docker-compose.yml:** ❌ Нет
- **env.example:** ✅ Есть
- **Зависимости:**
  - Auth Service (upstream на порт 8001)
  - Experiment Service (upstream на порт 8002)
- **Статус:** ✅ Реализован, но требует Auth Service для полной функциональности

### 3. Experiment Portal (Frontend)
- **Путь:** `projects/frontend/apps/experiment-portal/`
- **Технологии:** React 18, TypeScript, Vite, Nginx (production)
- **Порт:** 3000 (dev режим с Vite), 80 (production с Nginx)
- **Dockerfile:** ✅ Есть (`projects/frontend/apps/experiment-portal/Dockerfile`)
- **docker-compose.yml:** ✅ Есть (в корне проекта)
- **env.example:** ❌ Нет (использует переменные Vite)
- **Зависимости:**
  - Auth Proxy или Experiment Service (для API запросов)
- **Статус:** ✅ Реализован, готов к запуску
- **Примечание:** Для доступа извне контейнера в `vite.config.ts` настроен `host: '0.0.0.0'`

## Отсутствующие сервисы (упоминаются в документации)

### 4. Auth Service
- **Путь:** `projects/backend/services/auth-service/`
- **Технологии:** Python 3.14, aiohttp 3.10, asyncpg 0.31, PostgreSQL, PyJWT, bcrypt
- **Порт:** 8001
- **Dockerfile:** ✅ Есть (`projects/backend/services/auth-service/Dockerfile`)
- **env.example:** ✅ Есть
- **Зависимости:**
  - PostgreSQL 16 (база данных `auth_db`)
- **Статус:** ✅ Полностью реализован и готов к запуску
- **API Endpoints:**
  - `POST /auth/register` - регистрация пользователя
  - `POST /auth/login` - вход пользователя
  - `POST /auth/refresh` - обновление токена
  - `POST /auth/logout` - выход
  - `GET /auth/me` - информация о текущем пользователе
  - `GET /health` - health check

### 5. Metrics Service
- **Упоминается в:** `docs/experiment-tracking-ts.md`, `docs/experiment-service-roadmap.md`
- **Статус:** ❌ Не реализован
- **Примечание:** Не критично для базового запуска

### 6. Artifact Service
- **Упоминается в:** `docs/experiment-tracking-ts.md`, `docs/experiment-service-roadmap.md`
- **Статус:** ❌ Не реализован
- **Примечание:** Не критично для базового запуска

### 7. Comparison Service
- **Упоминается в:** `docs/experiment-tracking-ts.md`
- **Статус:** ❌ Не реализован
- **Примечание:** Не критично для базового запуска

### 8. API Gateway
- **Упоминается в:** `docs/experiment-tracking-ts.md`, `docs/experiment-service-roadmap.md`
- **Статус:** ❌ Не реализован
- **Примечание:** Не критично для базового запуска, можно использовать прямые запросы к сервисам

### 9. Telemetry Ingest Service
- **Упоминается в:** `docs/experiment-tracking-ts.md`, `docs/experiment-service-roadmap.md`
- **Статус:** ❌ Не реализован
- **Примечание:** Не критично для базового запуска

## Инфраструктурные зависимости

### PostgreSQL
- **Версия:** 16 (используется в experiment-service)
- **Порт:** 5432
- **База данных:** `experiment_db`
- **Статус:** ✅ Используется, есть в docker-compose.yml experiment-service
- **Миграции:** ✅ Есть (`projects/backend/services/experiment-service/migrations/`)

### RabbitMQ
- **Упоминается в:** `env.example`, `settings.py`
- **URL:** `amqp://guest:guest@localhost:5672/`
- **Статус:** ⚠️ Упоминается, но не используется активно в текущей реализации
- **Примечание:** Возможно потребуется для будущих интеграций

### Redis
- **Упоминается в:** `env.example`, `settings.py` (как `telemetry_broker_url`)
- **URL:** `redis://localhost:6379/0`
- **Статус:** ⚠️ Упоминается, но не используется активно в текущей реализации
- **Примечание:** Возможно потребуется для telemetry broker

### OpenTelemetry
- **Упоминается в:** `env.example`, `settings.py`
- **Endpoint:** `http://localhost:4318`
- **Статус:** ⚠️ Опционально, для мониторинга

## Минимальный набор для локального запуска

### Обязательные компоненты:
1. ✅ **PostgreSQL** - для хранения данных experiment-service
2. ✅ **Experiment Service** - основной backend сервис
3. ✅ **Experiment Portal** - фронтенд приложение (опционально, можно тестировать через API)

### Опциональные компоненты:
4. ✅ **Auth Service** - сервис аутентификации (рекомендуется для полной функциональности)
5. ✅ **Auth Proxy** - BFF для фронтенда (требует Auth Service)

### Не требуются для базового запуска:
- RabbitMQ
- Redis
- Metrics Service
- Artifact Service
- Comparison Service
- API Gateway
- Telemetry Ingest Service

## Текущие Dockerfile'ы

| Сервис | Путь к Dockerfile | Статус |
|--------|-------------------|--------|
| Experiment Service | `projects/backend/services/experiment-service/Dockerfile` | ✅ Готов |
| Auth Service | `projects/backend/services/auth-service/Dockerfile` | ✅ Готов |
| Auth Proxy | `projects/frontend/apps/auth-proxy/Dockerfile` | ✅ Готов |
| Experiment Portal | `projects/frontend/apps/experiment-portal/Dockerfile` | ✅ Готов |

## Текущие docker-compose файлы

| Файл | Путь | Описание |
|------|------|----------|
| Основной | `docker-compose.yml` | Единый compose для всех сервисов |
| Override (dev) | `docker-compose.override.yml.example` | Пример для dev режима с hot-reload |
| Локальный | `projects/backend/services/experiment-service/docker-compose.yml` | Устаревший, использовать основной |

## Переменные окружения

### Experiment Service
- Файл: `projects/backend/services/experiment-service/env.example`
- Основные переменные:
  - `DATABASE_URL` - подключение к PostgreSQL
  - `AUTH_SERVICE_URL` - URL Auth Service (опционально)
  - `RABBITMQ_URL` - URL RabbitMQ (не используется активно)
  - `TELEMETRY_BROKER_URL` - URL Redis (не используется активно)

### Auth Service
- Файл: `projects/backend/services/auth-service/env.example`
- Основные переменные:
  - `DATABASE_URL` - подключение к PostgreSQL (база `auth_db`)
  - `JWT_SECRET` - секретный ключ для подписи JWT токенов
  - `ACCESS_TOKEN_TTL_SEC` - время жизни access токена (по умолчанию 900 сек)
  - `REFRESH_TOKEN_TTL_SEC` - время жизни refresh токена (по умолчанию 1209600 сек)
  - `BCRYPT_ROUNDS` - количество раундов для хеширования паролей

### Auth Proxy
- Файл: `projects/frontend/apps/auth-proxy/env.example`
- Основные переменные:
  - `TARGET_EXPERIMENT_URL` - URL Experiment Service
  - `AUTH_URL` - URL Auth Service
  - `CORS_ORIGINS` - разрешенные origins для CORS

### Experiment Portal
- Использует переменные Vite (префикс `VITE_`)
- Основная переменная: `VITE_API_URL` (по умолчанию `http://localhost:8002`)

## Рекомендации для следующего шага

1. **Создать единый docker-compose.yml** в корне проекта, который объединит:
   - PostgreSQL
   - Experiment Service
   - Auth Proxy (с заглушкой Auth Service или без него)
   - Experiment Portal

2. **Добавить поддержку hot-reload** для разработки:
   - Volume для исходного кода
   - Переменные окружения для dev режима

3. **Настроить сеть Docker** для взаимодействия сервисов

4. **Добавить healthcheck'и** для всех сервисов

5. **Создать .env файл** в корне проекта с общими настройками

6. **Добавить скрипты запуска** (Makefile или shell скрипты)

## Заметки

- ✅ Auth Service полностью реализован и готов к использованию
- Auth Proxy требует Auth Service для полной функциональности
- Experiment Service может работать без Auth Service, используя заголовки для отладки
- Большинство сервисов из roadmap еще не реализованы и не требуются для базового запуска
- RabbitMQ и Redis упоминаются, но не используются активно в текущей реализации
- PostgreSQL используется для двух баз данных: `experiment_db` (experiment-service) и `auth_db` (auth-service)







