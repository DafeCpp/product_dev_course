# Experiment Service Roadmap

Документ описывает независимый план развития Experiment Service в рамках платформы Experiment Tracking. Фокус — на возможностях самого сервиса, API-контрактах и взаимодействии с соседними компонентами, без привязки к учебным модулям или расписанию курса.

## Цели и принципы
- **Единый источник правды** для сущностей `Experiment`, `Run`, `CaptureSession`.
- **Прозрачные статусы** запусков с историей изменений и массовыми операциями.
- **Гибкая интеграция** с Telemetry Ingest, Metrics и Artifact сервисами через стабильные API.
- **Наблюдаемость и аудит**: трассировки, бизнес-метрики, события для внешних consumers.

## Метрики успеха
- P95 ответа API < 400 мс при 200 RPS.
- MTTR по инцидентам сервиса < 20 мин.
- ≥ 95% критичных операций покрыто интеграционными тестами.
- Zero-downtime миграции для всех новых схем.

## Текущее состояние (декабрь 2025)
- **✅ Завершено (Foundation):** блок Foundation полностью (миграции, CRUD для `Experiment/Run/CaptureSession`, idempotency, пагинация, OpenAPI, RBAC). Добавлены домены `Sensor` и `ConversionProfile`, статусные машины и покрытие тестами (`tests/test_api_*`). Auth Proxy/BFF реализован.
- **⚠️ Частично реализовано (Runs & Capture Management):** batch-update статусов запусков, расширенная сущность `CaptureSession` с порядковыми номерами и статусной машиной. Валидация переходов статусов реализована. Bulk tagging, проверки инвариантов на уровне бизнес-логики, webhooks, аудит-лог отсутствуют.
- **❌ Не реализовано:** Telemetry ingest (WS/SSE), bulk tagging, webhooks/Kafka события, фоновые задачи (worker), расширенные фильтры API, экспорт данных, бизнес-политики доступа, SLO/SLI мониторинг, полная интеграция OpenTelemetry, chaos-тесты, operational документация.
- **Зависимости:** сервис собран на `aiohttp 3.10`, `asyncpg 0.29`, `pydantic-settings 2.4`, `structlog`, тестируется через `pytest`, `pytest-aiohttp`, `yandex-taxi-testsuite[postgresql]`, кодоген осуществляется `openapi-generator-cli 7.17`.

## Дорожная карта

### 1. Foundation (итерации 1‑2)
- **Доменные модели и CRUD:** ✅ Реализовано. Доменные модели `Experiment`, `Run`, `CaptureSession`, базовые CRUD-ручки c привязкой к проектам. Реализовано в `experiment-service/api/routes/experiments.py`, `runs.py`, `capture_sessions.py`.
- **Валидация состояний и idempotency:** ✅ Реализовано. Валидация состояний (`draft → running → finished/failed/archived`) через статусные машины в `experiment-service/services/state_machine.py`, idempotency для повторных запросов через заголовок `Idempotency-Key` в `experiment-service/services/idempotency.py`.
- **RBAC-хуки:** ✅ Реализовано. RBAC-хуки (owner/editor/viewer) и enforcing project-level scope на всех ручках. Реализовано в `experiment-service/services/dependencies.py` через `require_current_user` и `ensure_project_access`.
- **Многоуровневый доступ к проектам:** ✅ Реализовано. К проекту могут иметь доступ несколько пользователей с разными ролями (owner/editor/viewer). При запросе списка проектов пользователь получает все доступные ему проекты (где он является участником через таблицу `project_members`). Реализовано в `auth-service/migrations/002_add_projects.sql` и API endpoints в `auth-service/api/routes/projects.py`.
- **Ограничение создания экспериментов:** ✅ Реализовано. Пользователи могут создавать эксперименты только для проектов, к которым у них есть доступ с ролью owner или editor (viewer имеет только права на чтение). Реализовано в `experiment-service/api/routes/experiments.py:103` через `require_role=("owner", "editor")`.
- **UI для управления доступом к проектам:** ❌ Требуется реализация. UI для управления участниками проектов (добавление пользователей, назначение ролей owner/editor/viewer, удаление участников). Доступ к управлению только для владельцев проектов. Бэкенд API готов в `auth-service/api/routes/projects.py` (endpoints: `GET /projects/{id}/members`, `POST /projects/{id}/members`, `PATCH /projects/{id}/members/{user_id}`, `DELETE /projects/{id}/members/{user_id}`).
- **Профиль пользователя:** ❌ Требуется реализация. Модальное окно с информацией о текущем пользователе (username, email, список проектов с ролями). API `GET /auth/me` готов.
- **Миграции:** ✅ Реализовано. Миграции SQL в `experiment-service/migrations/001_initial_schema.sql`, автоматическое применение при старте через `experiment-service/src/experiment_service/main.py:apply_migrations_on_startup`. Сиды для тестовых данных не реализованы.
- **OpenAPI v1:** ✅ Реализовано. OpenAPI спецификация в `experiment-service/openapi/`, генерация client SDK через `openapi-generator-cli 7.17`.

### 2. Runs & Capture Management (итерации 3‑4)
- **Массовые операции:** ✅ Частично реализовано. Batch-update статусов запусков реализовано в `POST /api/v1/runs:batch-status` (`experiment-service/api/routes/runs.py:161`). Bulk tagging не реализовано.
- **Расширенная сущность CaptureSession:** ✅ Реализовано. Несколько сессий на один запуск, статусная машина `draft/running/failed/succeeded/archived/backfilling`, порядковые номера (`ordinal_number`) и связь с Telemetry Ingest по `capture_session_id`. Реализовано в `experiment-service/migrations/001_initial_schema.sql:149` и `experiment-service/services/capture_sessions.py`.
- **Контроль доменных инвариантов:** ⚠️ Частично реализовано. Валидация переходов статусов реализована в `experiment-service/services/state_machine.py`. Проверки инвариантов (нельзя архивировать запуск при активных capture session, нельзя удалить датчик с активными сессиями) не реализованы на уровне бизнес-логики.
- **Webhook-триггеры:** ❌ Не реализовано. Webhook-триггеры `run.started`, `run.finished`, `capture.started`, `capture.completed` отсутствуют. Требуется реализация системы webhooks.
- **Аудит-лог действий пользователей:** ❌ Не реализовано. Таблица `capture_session_events` существует в схеме (`experiment-service/migrations/001_initial_schema.sql:173`), но не используется в API. Требуется реализация логирования действий пользователей с фиксацией роли.

### 3. Data Integrity & Scaling (итерации 5‑6)
- **Фоновые задачи (worker):** ❌ Не реализовано. Фоновые задачи для авто-закрытия зависших запусков и реконcиляции capture-сессий, а также мониторинга backfill-процессов отсутствуют. Требуется реализация worker-сервиса.
- **Индексы и денормализации:** ✅ Частично реализовано. Базовые индексы реализованы в миграциях (`experiment-service/migrations/001_initial_schema.sql`): `experiments_project_status_idx`, `experiments_tags_gin_idx`, `experiments_metadata_gin_idx`, `runs_project_status_idx`, `capture_sessions_project_status_idx`. Расширенные индексы для фильтров по времени и активным датчикам могут потребовать оптимизации.
- **Инварианты хранения:** ⚠️ Частично реализовано. Поле `archived` (boolean) существует в таблице `capture_sessions`, но soft delete не реализован на уровне API. Immutable запись историй статусов не реализована.
- **Песочница для нагрузочного тестирования:** ❌ Не реализовано. Песочница для нагрузочного тестирования (pgbench + воспроизведение телеметрии) отсутствует.
- **Контроль версий схем:** ⚠️ Частично реализовано. Миграции SQL существуют, но нет формального changelog и гарантий совместимости назад на 2 релиза. OpenAPI версионирование не реализовано.

### 4. Integrations & Collaboration (итерация 7)
- **Enforcement бизнес-политик:** ❌ Не реализовано. Raw-значения доступны только ролям с разрешением, настройка «physical-only» на уровне проекта не реализована. Требуется реализация политик доступа к данным.
- **Расширенные фильтры API:** ⚠️ Частично реализовано. Базовые фильтры по статусу, проекту, пагинация реализованы. Фильтры по git SHA, участникам, связанным датчикам, версиям профилей преобразования не реализованы.
- **Экспорт данных:** ❌ Не реализовано. Экспорт данных (JSON/CSV) с выбором слоя `raw/physical`, подписка Comparison Service на события обновлений отсутствуют.
- **Подписки на события:** ❌ Не реализовано. Подписки на события через Kafka/Redis Stream для API Gateway и внешних consumers (включая события `conversion_profile.applied`, `capture.backfill.finished`) отсутствуют.
- **Auth Proxy/BFF:** ✅ Реализовано. Auth Proxy существует в `projects/frontend/apps/auth-proxy/`, реализует единую точку для фронта с куками сессии, проксированием REST, CORS/CSRF. WebSocket/SSE проксирование и rate limiting требуют доработки.

### 5. Hardening & Launch (итерация 8+)
- **SLO/SLI мониторинг:** ❌ Не реализовано. SLO/SLI мониторинг (Prometheus): RPS, latency, error rate, очередь задач, доля успешных backfill-операций отсутствует. Требуется интеграция с Prometheus.
- **Трассировка (OpenTelemetry):** ⚠️ Частично реализовано. Структурированное логирование через `structlog` и `backend_common.middleware.trace` реализовано с поддержкой `trace_id` и `request_id`. Полная интеграция OpenTelemetry для критичных сценариев (`create_run`, `close_run`, `capture_start`, `backfill_apply`) не реализована.
- **Chaos-тесты и отказоустойчивость:** ❌ Не реализовано. Chaos-тесты транзакций, failover сценарии БД, реплика + бэкапы, перезапуск backfill без потери идемпотентности отсутствуют.
- **Документация:** ⚠️ Частично реализовано. Базовая документация существует (README, OpenAPI спецификация). Operational runbook, диаграммы взаимодействий (Telemetry Ingest, Conversion Profiles, Metrics), RFC на дальнейшие фичи требуют доработки.

## Зависимости и интерфейсы
- **Telemetry Ingest:** события начала/окончания capture-сессий, связывание датчиков с запусками, применение профилей конвертации и результаты backfill.
- **Metrics Service:** запросы на агрегации для карточек запусков, подписки на обновления.
- **Artifact Service:** webhooks о смене статуса артефактов, ссылки на approved модели.
- **API Gateway:** агрегированная выдача для внешних клиентов, rate limiting и аутентификация.
- **Auth Proxy/BFF:** хранение access/refresh в HttpOnly куках, валидация сессии для фронта, проксирование `/api/*` и WS/SSE к Experiment Service (и далее к Gateway), нормализация ошибок, CORS/CSRF.
