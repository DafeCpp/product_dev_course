# Техническое задание: система сбора и отображения экспериментов

> **Примечание:** документ обновлён по состоянию реализации. Каждый пункт помечен:
> ✅ — реализовано, ⚠️ — частично реализовано, ❌ — не реализовано / в backlog.
> Подробный трекинг прогресса — в `docs/experiment-tracking-status-and-roadmap.md`.

## 1. Введение
- **Назначение документа:** описать требования к платформе Experiment Tracking Platform (ETP), которую разрабатывают студенты курса для финального проекта.
- **Заказчик:** учебная программа «Продуктовая разработка бэкенда».
- **Стейкхолдеры:** кураторы курса, преподаватели, студенты (команды разработки).

## 1.1 Технологический стек

| Слой | Планировалось | Реализовано |
|------|--------------|-------------|
| Язык и HTTP-слой | Python 3.14+, `aiohttp` | ✅ Python 3.14, `aiohttp` (сервер и клиент) |
| Работа с БД | PostgreSQL 15+, `asyncpg`, без ORM; миграции через Alembic | ⚠️ PostgreSQL 16 + TimescaleDB, `asyncpg`, без ORM; **миграции — собственный runner** (`schema_migrations` + checksum), не Alembic |
| Фронтенд | TypeScript + React + Vite | ✅ TypeScript + React + Vite + React Router + React Query + Plotly |
| Стриминг/интеграции | WebSocket/SSE, Kafka или Redis Streams для Telemetry Ingest | ⚠️ **SSE + REST** (WebSocket и Kafka/Redis не реализованы; достаточно для MVP) |
| Инфраструктура | Docker/Docker Compose, nginx как фронтовой прокси | ⚠️ Docker Compose; **Auth Proxy (Fastify)** вместо nginx как BFF/прокси; nginx только внутри контейнеров фронтенда |
| Тестирование и качество | pytest, pytest-aiohttp, линтеры (ruff, mypy), OpenAPI/AsyncAPI | ✅ pytest + pytest-aiohttp + yandex-taxi-testsuite; vitest (фронт); jest (auth-proxy); mypy; Cypress (E2E); OpenAPI — ✅, AsyncAPI — ❌ |

## 2. Цели продукта и KPI
1. **Прозрачность экспериментов:** ✅ каждый эксперимент и его запуски доступны для просмотра в единой системе.
2. **Отслеживаемость качества:** ⚠️ просмотр телеметрии (live + historical) и аудит-лог реализованы; сравнение запусков (Comparison Service) — ❌.
3. **Повторяемость:** ⚠️ параметры, окружение, git_sha сохраняются; артефакты — только схема, без хранилища.
4. **KPI:**
   - покрытие 100% экспериментов командой — ✅ (все эксперименты привязаны к проектам с RBAC)
   - TTM сравнения < 10 сек — ❌ (Comparison Service не реализован)
   - время регистрации нового запуска < 3 сек — ✅
   - MTTR < 15 мин — ⚠️ (OpenTelemetry + Grafana/Loki для диагностики, но SLO/SLI мониторинг не настроен)

## 3. Пользовательские роли и сценарии
- **Оператор экспериментов (Data Scientist):** ✅ логинится, управляет экспериментами и запусками, просматривает телеметрию.
- **Инженер устройств (Device Integrator / MLOps):** ✅ регистрирует датчики, настраивает подключение; sensor-simulator позволяет тестировать.
- **Аудитор (read-only):** ✅ роль `viewer` — только чтение, без модификации данных.

### 3.1 Базовый сценарий работы
1. ✅ Все датчики непрерывно отправляют телеметрию в публичную ingest-ручку (`POST /api/v1/telemetry`).
2. ⚠️ Пользователь авторизуется и видит список датчиков с фильтрацией; дашборд онлайн-статусов (heartbeat sparklines) — ❌.
3. ✅ Пользователь может добавить новый датчик (имя, тип, токен, единица измерения); тестовая отправка через sensor-simulator.
4. ✅ Пользователь открывает/создаёт эксперимент, добавляет запуск.
5. ✅ Запуск переводится в `running`; пользователь наблюдает live-графики (SSE, мульти-панельный TelemetryViewer с Plotly, raw/physical).
6. ✅ Кнопки «Старт отсчёта» / «Стоп отсчёта» создают/завершают capture session; данные сохраняются.
7. ✅ Несколько capture session на один запуск; пользователь выбирает нужную для просмотра.
8. ✅ После завершения — режим history с пагинацией, фильтрацией по сенсорам, агрегированный просмотр (1m downsampling); экспорт CSV/JSON.

## 4. Объем поставки

### В scope (реализовано)
- ✅ Auth Service (Python/aiohttp)
- ✅ Auth Proxy / BFF (Fastify/TypeScript)
- ✅ Experiment Service (CRUD экспериментов/запусков/сессий, датчики, webhooks, аудит, фоновые задачи)
- ✅ Telemetry Ingest Service (REST ingest, SSE стриминг, исторические запросы, агрегированные запросы)
- ✅ Frontend SPA (React, полный CRUD, live + historical телеметрия, webhooks, аудит, управление проектами/пользователями)
- ✅ Sensor Simulator (React SPA — генерация тестовой телеметрии с различными сценариями)
- ✅ Docker Compose окружение
- ✅ Тесты (unit + integration: pytest, vitest, jest, Cypress)
- ✅ OpenAPI документация
- ✅ Импорт/экспорт через REST (CSV/JSON)
- ✅ Logging stack (Loki + Alloy + Grafana)
- ✅ OpenTelemetry трассировка

### Вне scope / не реализовано
- ❌ Metrics Service (отдельный — метрики `run_metrics` есть в схеме, но без API/UI)
- ❌ Artifact Service (таблица `artifacts` есть, но нет S3/pre-signed URL/UI)
- ❌ Comparison Service
- ❌ API Gateway (роль выполняет Auth Proxy)
- ❌ WebSocket (вместо него SSE — достаточно для MVP)
- ❌ Kafka/Redis Streams
- ❌ Kubernetes деплой
- ❌ Мобильное приложение
- ❌ AsyncAPI спецификация

## 5. Архитектура и компоненты

| Компонент | Ответственность | Хранение данных | Статус |
|-----------|-----------------|-----------------|--------|
| Auth Service | Регистрация, логин, JWT (access+refresh), проекты, участники с ролями | PostgreSQL (`auth_db`) | ✅ |
| Auth Proxy (BFF) | Единая точка для фронта: login/logout/refresh через HttpOnly-куки, прокси `/api/*` → experiment-service, `/api/v1/telemetry/*` → telemetry-ingest, `/projects/*` → auth-service; CORS, CSRF, rate limiting, SSE-проксирование | Стейт в куках, без БД | ✅ |
| Experiment Service | CRUD экспериментов, запусков, capture sessions, датчиков, conversion profiles; статусные машины, idempotency, webhooks (outbox + dispatcher), аудит-лог, фоновые задачи, экспорт | PostgreSQL/TimescaleDB (`experiment_db`) | ✅ |
| Telemetry Ingest Service | Приём телеметрии (REST), SSE стриминг, исторические и агрегированные запросы; авторизация по sensor token и user JWT | PostgreSQL/TimescaleDB (общая `experiment_db`) | ✅ |
| Frontend (Experiment Portal) | React SPA: проекты, эксперименты, запуски, датчики, телеметрия (live + history), webhooks, аудит | Работает поверх Auth Proxy | ✅ |
| Sensor Simulator | React SPA для генерации тестовой телеметрии (waveforms, сценарии burst/dropout/late) | localStorage | ✅ |
| Logging Stack (Loki + Alloy + Grafana) | Сбор и визуализация логов со всех сервисов | Loki | ✅ |
| Metrics Service | Прием батчей метрик, хранение серий, агрегации | — | ❌ |
| Artifact Service | Загрузка артефактов, версионирование | — | ❌ |
| Comparison Service | Построение сравнений запусков | — | ❌ |
| API Gateway | Агрегация ответов, внешний API | — | ❌ (Auth Proxy выполняет роль BFF) |

## 6. Функциональные требования

### 6.1 Управление аутентификацией и проектами
- ✅ Регистрация пользователя (открытая, без инвайт-кода).
- ❌ Восстановление пароля.
- ✅ Создание проектов, добавление участников с ролями `owner`, `editor`, `viewer`.
- ✅ **Многоуровневый доступ к проектам:** при `GET /projects` пользователь получает все проекты, где он участник через `project_members`.
- ❌ Поиск и фильтрация проектов через UI (есть список, но без поиска/фильтра).
- ✅ JWT авторизация (access + refresh токены, PyJWT, bcrypt).
- ❌ Ротация refresh токенов с черным списком при отзыве (logout — stub).
- ✅ Auth Proxy (BFF) на **Fastify** (TypeScript): `/auth/login`, `/auth/refresh`, `/auth/logout`, `/auth/me`; прокси `/api/*`, `/projects/*`, `/api/v1/telemetry/*`; access/refresh в HttpOnly Secure SameSite=Lax куках.
- ✅ CSRF: double-submit cookie (`csrf_token`) + проверка `Origin/Referer` + `X-CSRF-Token`.
- ✅ CORS: whitelist фронтовых origin, `credentials: true`.
- ✅ Rate limiting на логин и по сессии (`@fastify/rate-limit`).

#### Инварианты прав доступа
- ✅ Каждая сущность принадлежит проекту; пользователь должен состоять в проекте.
- ✅ `owner`: управляет участниками и ролями, может удалять проект/датчики/эксперименты, ротировать токены датчиков.
- ✅ `editor`: создаёт/редактирует эксперименты, запуски, capture sessions; регистрирует датчики.
- ✅ `viewer`: доступ только на чтение.
- ✅ Операции с capture session логируются с user_id и ролью (audit-log).
- ❌ Raw-значения ограничены политиками проекта (доступны всем ролям).

#### Инварианты целостности данных
- ✅ Датчик нельзя удалить при активных capture sessions.
- ⚠️ Эксперимент можно архивировать — нет проверки на `running` запуски (в backlog).
- ✅ Capture session нельзя удалить, кроме `draft`/`failed`.
- ⚠️ Профили преобразования — схема и статусная машина есть; backfill с пересчётом `physical_value` — ❌.

### 6.2 Датчики и телеметрия
- ✅ Регистрация датчика: имя, тип, единица измерения, проект, секретный токен.
- ⚠️ API ingest `/api/v1/telemetry` — POST (REST, батчи); SSE стриминг (`GET /api/v1/telemetry/stream`). **WebSocket — ❌.**
- ❌ SLA ingest API (rate limit на уровне токена, 429, журнал датчика).
- ⚠️ Мониторинг состояния: `last_heartbeat` хранится; автоматический расчёт `online`/`delayed`/`offline` — ❌.
- ✅ Веб-интерфейс: список датчиков, создание, фильтрация по проекту/типу.
- ✅ Sensor Simulator (React SPA) для тестовой отправки с различными сценариями (burst, dropout, sine, late data).
- ❌ Журнал ошибок приёма на уровне датчика.
- ✅ Каждое значение хранит `raw_value` и `physical_value`; пользователь переключает режим на графике.
- ⚠️ Профили преобразования: схема + статусная машина (`draft`, `active`, `scheduled`, `deprecated`); backfill-пересчёт — ❌.
- ✅ Множественные проекты для датчиков (`sensor_projects`).
- ✅ Агрегированные запросы из `telemetry_1m` continuous aggregate (TimescaleDB).

### 6.3 Эксперименты и запуски
- ✅ CRUD экспериментов (название, описание, проект, статус, теги, metadata).
- ✅ Создание запусков (run) с параметрами: git_sha, env, tags, params (JSONB), status.
- ✅ Статусы: `draft`, `running`, `failed`, `succeeded`, `archived`.
- ⚠️ Привязка датчиков к запуску — таблица `run_sensors` есть, UI ограничен.
- ✅ Массовое обновление статуса запусков (`POST /api/v1/runs:batch-status`).
- ✅ Bulk tagging (`POST /api/v1/runs:bulk-tags`).
- ✅ «Старт отсчёта» / «Стоп отсчёта» создаёт/завершает capture session.
- ✅ Один запуск поддерживает несколько capture sessions.
- ✅ Расширенные фильтры: `status`, `tags`, `created_after`, `created_before`, поиск по тексту.
- ✅ Экспорт экспериментов и запусков в CSV/JSON.

### 6.4 Метрики
- ⚠️ Таблица `run_metrics` существует в схеме (run_id, name, step, value, timestamp).
- ❌ Endpoint `POST /runs/{id}/metrics` — не реализован.
- ❌ Агрегации min/avg/max по шагам — не реализованы.
- ✅ Live-канал (SSE) ретранслирует последние точки телеметрии.
- ✅ При активном отсчёте сохраняются все значения датчиков с отметками времени.
- ✅ Для каждого значения доступны `raw_value` и `physical_value`.
- ❌ Backfill engine для пересчёта `physical_value` при смене профиля.

### 6.5 Артефакты
- ⚠️ Таблица `artifacts` существует в схеме (type, uri, checksum, size, metadata, approved_by).
- ❌ Загрузка файлов через pre-signed URL / S3.
- ❌ UI для управления артефактами.

### 6.6 Сравнения
- ❌ Comparison Service не реализован.
- ❌ Сравнение запусков, расчёт дельт, экспорт отчётов.

### 6.7 API Gateway
- ❌ Отдельный API Gateway не реализован; Auth Proxy (Fastify) выполняет роль BFF/прокси.
- ⚠️ Rate limiting реализован в Auth Proxy, но не per-token, а per-session/IP.

### 6.8 Frontend
- ✅ Страницы: список проектов, список экспериментов, карточка эксперимента, детали запуска, список датчиков, создание датчика, телеметрия, webhooks.
- ✅ Одновременное отображение нескольких графиков (мульти-панельный TelemetryViewer с drag-and-drop, Plotly).
- ✅ Панель управления запуском: кнопки «Старт отсчёта» / «Стоп отсчёта».
- ✅ Переключатель capture session для выбранного запуска (history mode).
- ✅ Формы добавления датчика, эксперимента и запуска с валидацией.
- ❌ Монитор датчиков (online/offline, heartbeat sparklines) — не реализован.
- ❌ Экран сравнения запусков.

### 6.9 UX Live-монитора
- ❌ Таймлайн capture session поверх графиков (цветные полосы running/failed/backfilling).
- ✅ Легенда на каждом графике, raw/physical переключение.
- ❌ Правая панель с мини-спарклайнами, задержкой, heartbeat.
- ✅ Toast-уведомления об ошибках (DebugErrorToast с Details/Copy, корреляция trace-id/request-id).
- ❌ Обратный отсчёт до автозавершения, блокировка повторного нажатия.

### 6.10 Тестовые данные и демо-стенд
- ✅ Sensor Simulator (React SPA) — заменяет CLI `demo-sensor`; генерирует шаблонные сигналы (sine, bursts, saw), поддерживает мульти-сенсор конфигурацию.
- ❌ Фикстуры `demo_project` с заранее записанными capture sessions.
- ❌ Скрипт `bin/demo_seed.py`.
- ✅ Docker Compose: `docker-compose up` разворачивает полный стенд.
- ✅ Документация: `docs/demo-flow.md`, `docs/manual-testing.md`, `docs/local-dev-docker-setup.md`.

### 6.11 Производительность фронтенда
- ⚠️ Время первой отрисовки — не замерялось формально; Vite build + code splitting используются.
- ⚠️ Графики поддерживают до 20k точек (Plotly `scattergl`); web workers — ❌.
- ⚠️ Ресурсные лимиты: `historyMaxPoints` (default 5000, max 20000); автоматическая агрегация через TimescaleDB continuous aggregate (1m downsampling).

### 6.12 Безопасность и аудит
- ✅ Секреты датчиков: токен показывается один раз при создании; ротация через `POST /sensors/{id}/rotate-token`.
- ❌ MFA для ротации токенов.
- ✅ Аудит-лог: операции с capture sessions и запусками (user_id, роль, payload).
- ❌ Хранение аудита минимум 1 год (без настроенного retention).
- ⚠️ TLS: делегировано на уровень деплоя (не настроен в Docker Compose).
- ❌ AES-256 для токенов/профилей в БД; KMS.
- ✅ Маскирование секретов в UI (DebugErrorToast, httpDebug.ts).

### 6.13 Auth Proxy / BFF требования
- ✅ `/auth/login`, `/auth/refresh`, `/auth/logout`, `/auth/me`.
- ✅ Прокси `/api/*` → experiment-service, `/api/v1/telemetry/*` → telemetry-ingest-service, `/projects/*` → auth-service.
- ✅ SSE-проксирование (hardened headers).
- ✅ Access/refresh в HttpOnly Secure SameSite=Lax куках.
- ✅ CSRF: double-submit cookie + `X-CSRF-Token` + проверка `Origin/Referer`.
- ✅ CORS whitelist.
- ✅ Rate limiting (`@fastify/rate-limit`).
- ✅ Заголовки: `X-Request-Id`, `X-Trace-Id`, `X-User-Id`, `X-Project-Id`, `X-Project-Role`.

### 6.14 Auth Proxy / BFF стек
- ✅ **Fastify + TypeScript** (с плагинами `@fastify/http-proxy`, `@fastify/cors`, `@fastify/rate-limit`, `@fastify/cookie`).
- ✅ Маршруты: `/auth/*`, `/api/*`, `/projects/*`, `/api/v1/telemetry/*`.
- ✅ Хранение сессий: access/refresh в HttpOnly cookies.
- ✅ Наблюдаемость: `request_id`, `trace_id` в логах.

## 7. API и интеграции
- ✅ Experiment Service и Auth Service описывают OpenAPI спецификации.
- ❌ API Gateway с объединённой схемой.
- ✅ Telemetry Ingest Service: REST-ручка `/api/v1/telemetry` и SSE `/api/v1/telemetry/stream`.
- ❌ AsyncAPI описание.
- ✅ Webhooks: исходящие уведомления по событиям (status change, backfill и т.д.) через webhook subscriptions.
- ❌ Входящие вебхуки для CI/CD.
- ❌ CLI-утилита для отправки результатов.

## 8. Данные и модели
- ✅ **Experiment:** id, project_id, name, description, tags[], owner_id, created_at, updated_at, status, experiment_type, metadata (JSONB).
- ✅ **Run:** id, experiment_id, project_id, params(JSONB), git_sha, env, tags[], status, started_at, finished_at, duration_seconds, notes, metadata.
- ⚠️ **RunMetric:** run_id, name, step, value, timestamp — таблица есть, API нет.
- ⚠️ **Artifact:** id, run_id, type, uri, checksum, size, metadata, approved_by — таблица есть, API нет.
- ❌ **Comparison:** не реализовано.
- ✅ **Sensor:** id, project_id, name, type, input_unit, display_unit, token_hash, token_preview, status, last_heartbeat, active_profile_id.
- ✅ **SensorProjects:** sensor_id, project_id (M2M).
- ✅ **ConversionProfile:** id, sensor_id, version, kind, payload(JSONB), valid_from, valid_to, status, created_by, published_by.
- ✅ **TelemetryRecord:** id, sensor_id, run_id, capture_session_id, timestamp, raw_value, physical_value, signal (generated), conversion_status, meta (JSONB). Хранится в TimescaleDB hypertable.
- ✅ **CaptureSession:** id, run_id, ordinal_number, started_at, stopped_at, status (draft/running/succeeded/failed/archived/backfilling), initiated_by, notes.
- ✅ **WebhookSubscription / WebhookDelivery:** outbox pattern, event_types, retry, dedup.
- ✅ **RunEvents / CaptureSessionEvents:** аудит-лог.
- ✅ **RequestIdempotency:** idempotency_key, user_id, request/response cache.
- ⚠️ Миграции: **собственный runner** (`schema_migrations` + SHA256 checksum, SQL-файлы); Alembic не используется.

## 9. Нефункциональные требования
- ⚠️ **Производительность:** P95 latency < 400 мс — не замерялось под нагрузкой; базовые индексы и TimescaleDB оптимизации настроены.
- ⚠️ **Надёжность:** целевой аптайм 99.5% — не SLA; бэкапы БД — не настроены (ответственность деплоя).
- ⚠️ **Масштабируемость:** сервисы stateless (могут горизонтально масштабироваться); шардирование — не реализовано.
- ⚠️ **Безопасность:** TLS на уровне деплоя; CSRF + SameSite cookies; маскирование секретов; шифрование at rest — ❌.
- ✅ **Соответствие:** аудит-лог для run/capture session действий; retention — не настроен.
- ⚠️ **Поток датчиков:** 200 одновременных датчиков, 5k точек/сек — не нагрузочно-тестировалось; TimescaleDB + batch insert + SSE poll.

## 10. Наблюдаемость и поддержка
- ❌ Метрики Prometheus (RPS, latency, error rate, queue lag).
- ✅ OpenTelemetry трассировка для experiment-service (TracerProvider + OTLP HTTP exporter + auto-instrumentation).
- ✅ Структурированное логирование (structlog, JSON) → Alloy → Loki → Grafana.
- ✅ Дашборд логов в Grafana (`infrastructure/logging/grafana/dashboards/logs-overview.json`).
- ❌ Алёрты (error rate, latency, disk usage).

## 11. Управление изменениями
- ❌ RFC-процесс для изменений контракта API (используются ADR: `docs/adr/`).
- ✅ Версионирование API: `/api/v1`.
- ❌ Feature flags.

## 12. План релизов

| Этап | Содержание | Статус |
|------|-----------|--------|
| **MVP (спринты 1–3)** | Auth, Telemetry Ingest (REST + SSE), список датчиков, CRUD экспериментов/запусков, старт/стоп отсчёта, Docker Compose | ✅ |
| **Beta (спринты 4–6)** | SSE стриминг (live + history), мульти-панельные графики, webhooks, аудит, расширенные фильтры, экспорт, backfill, RBAC, TimescaleDB | ✅ |
| **GA (спринты 7–8)** | Наблюдаемость (OpenTelemetry + Loki/Grafana), документация | ⚠️ (трассировка и логи — ✅; SLO/SLI, алёрты, CLI, вебхуки CI/CD — ❌) |
| **Не реализовано** | Metrics Service (отдельный), Artifact Service (S3), Comparison Service, API Gateway, WebSocket, Kafka, мобильное приложение, chaos-тесты, нагрузочное тестирование | ❌ |

## 13. Критерии приёмки

| Критерий | Статус |
|----------|--------|
| Все обязательные сервисы разворачиваются `docker-compose up` без ручных правок | ✅ |
| 80% функционала покрыто авто-тестами (unit + integration) | ⚠️ (покрытие есть, но не замерялось процентно) |
| Описаны и протестированы основные сценарии из раздела 3 | ✅ (`docs/manual-testing.md`, `docs/demo-flow.md`) |
| Документация (README + OpenAPI + ADR) актуальна | ✅ |
| Демо: поток от минимум 3 датчиков, live-графики, цикл «Старт/Стоп отсчёта» с сохранением данных | ✅ (sensor-simulator + TelemetryViewer) |
