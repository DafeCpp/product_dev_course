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

## Текущее состояние (актуализируется)
- **✅ Завершено (Foundation):** блок Foundation полностью (миграции, CRUD для `Experiment/Run/CaptureSession`, idempotency, пагинация, OpenAPI, RBAC). Добавлены домены `Sensor` и `ConversionProfile`, статусные машины и покрытие тестами (`tests/test_api_*`). Множественные проекты для датчиков реализованы полностью (backend: миграция `002_sensor_projects_many_to_many.sql`, API endpoints, тесты; frontend: UI для управления проектами в `SensorDetail.tsx`). UI для управления доступом к проектам реализован (`ProjectMembersModal` с тестами). Профиль пользователя реализован (`UserProfileModal` с тестами).
- **⚠️ Частично реализовано (Runs & Capture Management):** batch-update статусов запусков, расширенная сущность `CaptureSession` с порядковыми номерами и статусной машиной. Валидация переходов статусов реализована. Bulk tagging, проверки инвариантов на уровне бизнес-логики, webhooks, аудит-лог отсутствуют.
- **⚠️ Частично реализовано (Frontend):** UI для управления участниками проектов реализован (`ProjectMembersModal`). Профиль пользователя реализован (`UserProfileModal`). Множественные проекты для датчиков реализованы (UI в `SensorDetail.tsx`). Фильтрация экспериментов по проектам с выпадающим списком реализована (`ExperimentsList.tsx`). Фильтрация датчиков по проектам реализована с выпадающим списком (`SensorsList.tsx`). **Модальное окно для просмотра/редактирования проектов реализовано** (единый `ProjectModal` для create/view/edit, интегрирован в `ProjectsList.tsx`). **Автозаполнение проекта при создании эксперимента реализовано** (через `ExperimentsList.tsx → CreateExperimentModal` + `active_project_id` в localStorage). Множественные проекты при создании датчика не реализованы.
- **⚠️ Частично реализовано (Auth Proxy/BFF):** Auth Proxy реализует единую точку для фронта с куками сессии, проксированием REST, CORS, rate limiting, WebSocket поддержкой. SSE проксирование и CSRF защита требуют доработки.
- **❌ Не реализовано:** Telemetry ingest (WS/SSE), bulk tagging, webhooks/Kafka события, фоновые задачи (worker), расширенные фильтры API, экспорт данных, бизнес-политики доступа, SLO/SLI мониторинг, полная интеграция OpenTelemetry, chaos-тесты, operational документация.
  - Примечание: **REST ingest** реализован отдельным сервисом `telemetry-ingest-service` (`POST /api/v1/telemetry`); WS/SSE режимы и полноценная интеграция со сценариями backfill/реплея — в backlog.
- **Зависимости:** сервис собран на `aiohttp 3.10`, `asyncpg 0.29`, `pydantic-settings 2.4`, `structlog`, тестируется через `pytest`, `pytest-aiohttp`, `yandex-taxi-testsuite[postgresql]`, кодоген осуществляется `openapi-generator-cli 7.17`.

## Дорожная карта

### 1. Foundation (итерации 1‑2)
- **Доменные модели и CRUD:** ✅ Реализовано. Доменные модели `Experiment`, `Run`, `CaptureSession`, базовые CRUD-ручки c привязкой к проектам. Реализовано в `experiment-service/api/routes/experiments.py`, `runs.py`, `capture_sessions.py`.
- **Множественные проекты для датчиков:** ✅ Реализовано.
  - ✅ **Backend:** Реализовано. Миграция БД для создания таблицы связи `sensor_projects` (`experiment-service/migrations/002_sensor_projects_many_to_many.sql`), обновление доменной модели `Sensor`, API endpoints для управления связями (`POST /api/v1/sensors/{id}/projects`, `DELETE /api/v1/sensors/{id}/projects/{project_id}`, `GET /api/v1/sensors/{id}/projects`), обновление RBAC-проверок для учета множественных проектов, обновление фильтрации датчиков по проектам. Реализовано в `experiment-service/src/experiment_service/repositories/sensors.py`, `services/sensors.py`, `api/routes/sensors.py`. Покрыто тестами (`tests/test_api_sensors.py`).
  - ✅ **Frontend:** Реализовано. Добавлены методы в API клиент (`sensorsApi.addProject`, `sensorsApi.removeProject`, `sensorsApi.getProjects`). Обновлен `SensorDetail.tsx` для отображения списка проектов датчика с возможностью добавления и удаления проектов. Реализовано модальное окно для добавления проекта, таблица со списком проектов, защита от удаления основного проекта. Стили добавлены в `SensorDetail.css`. Реализовано в `projects/frontend/apps/experiment-portal/src/pages/SensorDetail.tsx` и `api/client.ts`.
- **Фильтрация датчиков по доступным проектам:** ⚠️ Частично реализовано (backend готов, frontend требует доработки).
  - ✅ **Backend:** Реализовано. API endpoint `GET /api/v1/sensors` поддерживает опциональный параметр `project_id`. Если `project_id` не указан, используется `active_project_id` из заголовков. Логика фильтрации обновлена в репозитории и сервисе датчиков для работы с множественными проектами через таблицу `sensor_projects`. Реализовано в `experiment-service/src/experiment_service/api/routes/sensors.py:87`.
  - ✅ **Frontend:** Реализовано. В `SensorsList.tsx` добавлен выпадающий список проектов с автоматическим выбором первого доступного проекта. Реализовано в `projects/frontend/apps/experiment-portal/src/pages/SensorsList.tsx`.
- **Множественные проекты при создании датчика:** ✅ Реализовано. В UI `CreateSensor.tsx` добавлен multi-select проектов: первый выбранный проект становится основным (`project_id` при `POST /api/v1/sensors`), а остальные проекты добавляются сразу после создания через `POST /api/v1/sensors/{id}/projects`.
- **Валидация состояний и idempotency:** ✅ Реализовано. Валидация состояний (`draft → running → finished/failed/archived`) через статусные машины в `experiment-service/services/state_machine.py`, idempotency для повторных запросов через заголовок `Idempotency-Key` в `experiment-service/services/idempotency.py`.
- **RBAC-хуки:** ✅ Реализовано. RBAC-хуки (owner/editor/viewer) и enforcing project-level scope на всех ручках. Реализовано в `experiment-service/services/dependencies.py` через `require_current_user` и `ensure_project_access`.
- **Многоуровневый доступ к проектам:** ✅ Реализовано. К проекту могут иметь доступ несколько пользователей с разными ролями (owner/editor/viewer). При запросе списка проектов пользователь получает все доступные ему проекты (где он является участником через таблицу `project_members`). Реализовано в `auth-service/migrations/002_add_projects.sql` и API endpoints в `auth-service/api/routes/projects.py`.
- **Ограничение создания экспериментов:** ✅ Реализовано. Пользователи могут создавать эксперименты только для проектов, к которым у них есть доступ с ролью owner или editor (viewer имеет только права на чтение). Реализовано в `experiment-service/api/routes/experiments.py:103` через `require_role=("owner", "editor")`.
- **Автозаполнение проекта при создании эксперимента:** ✅ Реализовано. При создании эксперимента из списка экспериментов выбранный в фильтрах проект передается как `defaultProjectId` в `CreateExperimentModal`, а также используется fallback на последний активный проект (`active_project_id` в localStorage), если `defaultProjectId` не задан.
- **UI для управления доступом к проектам:** ✅ Реализовано. UI для управления участниками проектов реализован в компоненте `ProjectMembersModal.tsx` (`projects/frontend/apps/experiment-portal/src/components/ProjectMembersModal.tsx`). Компонент интегрирован в `ProjectsList.tsx` с кнопкой управления участниками (видна только для владельцев проектов). Функциональность включает: просмотр списка участников, добавление пользователей, изменение ролей (owner/editor/viewer), удаление участников. Покрыто тестами (`ProjectMembersModal.test.tsx`). Backend API готов в `auth-service/api/routes/projects.py`.
- **Модальное окно для просмотра и редактирования проектов:** ✅ Реализовано. Добавлен единый `ProjectModal` (режимы create/view/edit) с role gating (редактирование доступно owner/editor), интегрирован в `ProjectsList.tsx`. Бэкенд API используется через `GET /projects/{id}` и `PUT /projects/{id}` (auth-service).
- **Профиль пользователя:** ✅ Реализовано. Модальное окно с информацией о текущем пользователе реализовано в компоненте `UserProfileModal.tsx` (`projects/frontend/apps/experiment-portal/src/components/UserProfileModal.tsx`). Компонент интегрирован в `Layout.tsx` с кликабельным username в header. Функциональность включает: отображение информации о пользователе (ID, username, email, дата регистрации), список проектов с ролями пользователя (owner/editor/viewer), получение ролей через `projectsApi.listMembers` для каждого проекта. Покрыто тестами (`UserProfileModal.test.tsx`). API `GET /auth/me` готов.
- **Миграции:** ✅ Реализовано. Миграции SQL в `experiment-service/migrations/001_initial_schema.sql`, автоматическое применение при старте через `experiment-service/src/experiment_service/main.py:apply_migrations_on_startup`. Сиды для тестовых данных не реализованы.
- **OpenAPI v1:** ✅ Реализовано. OpenAPI спецификация в `experiment-service/openapi/`, генерация client SDK через `openapi-generator-cli 7.17`.

### 2. Runs & Capture Management (итерации 3‑4)
- **Массовые операции:** ✅ Реализовано. Batch-update статусов запусков реализовано в `POST /api/v1/runs:batch-status` (`experiment-service/api/routes/runs.py:161`). Bulk tagging реализовано (см. пункт ниже).
- **Bulk tagging:** ✅ Реализовано. Добавлен endpoint `POST /api/v1/runs:bulk-tags` (операции `add_tags`/`remove_tags` или `set_tags` для списка `run_ids`), с проверкой ролей owner/editor и project scoping.
- **Расширенная сущность CaptureSession:** ✅ Реализовано. Несколько сессий на один запуск, статусная машина `draft/running/failed/succeeded/archived/backfilling`, порядковые номера (`ordinal_number`) и связь с Telemetry Ingest по `capture_session_id`. Реализовано в `experiment-service/migrations/001_initial_schema.sql:149` и `experiment-service/services/capture_sessions.py`.
- **Контроль доменных инвариантов:** ⚠️ Частично реализовано. Валидация переходов статусов реализована в `experiment-service/services/state_machine.py`.
  - ✅ **Инвариант 1:** нельзя перевести `Run` в `succeeded/failed/archived`, если есть активные `CaptureSession` (`draft/running/backfilling`); применяется в `PATCH /api/v1/runs/{id}` и `POST /api/v1/runs:batch-status`.
  - ✅ **Инвариант 2:** нельзя удалить `Sensor`, если он участвует в активных `CaptureSession` (через `run_sensors → capture_sessions`).
  - ✅ **Инвариант 3:** нельзя удалить `CaptureSession`, если она активна (`running/backfilling`); удаление допустимо после завершения.
- **Webhook-триггеры:** ⚠️ Частично реализовано. Добавлена система webhooks на базе outbox (`webhook_deliveries`) + подписок (`webhook_subscriptions`) и фонового dispatcher’а.
  - ✅ CRUD подписок: `GET/POST/DELETE /api/v1/webhooks` (owner/editor для записи; viewer+ для чтения).
  - ✅ Триггеры:
    - `capture_session.created` и `capture_session.stopped` (на create/stop capture session).
    - `run.started`, `run.finished`, `run.archived` (на `PATCH /api/v1/runs/{id}` и `POST /api/v1/runs:batch-status`).
  - ⚠️ Delivery гарантии: best-effort, at-least-once; retry с backoff до `webhook_max_attempts`. Расширенные политики (DLQ, дедупликация, rate limit per target) — в backlog.
- **Аудит-лог действий пользователей:** ⚠️ Частично реализовано. Таблица `capture_session_events` используется для аудита старт/стоп capture-сессий с фиксацией `actor_id` и `actor_role`.
  - ✅ Запись событий на `POST /api/v1/runs/{run_id}/capture-sessions` (`capture_session.created`) и `POST /api/v1/runs/{run_id}/capture-sessions/{session_id}/stop` (`capture_session.stopped`).
  - ✅ Чтение событий: `GET /api/v1/runs/{run_id}/capture-sessions/{session_id}/events` (доступно viewer+).
  - ❌ Полный audit trail по остальным сущностям/операциям (runs/experiments/sensors/artifacts), а также гарантии неизменяемости/retention — в backlog.
- **Догрузка данных после завершения (late/backfill ingest):** ❌ Не реализовано. Нужна возможность добавлять телеметрию в `capture_session` **после** завершения эксперимента (например, часть данных копится на устройстве и выгружается только в конце). Требуется:
  - принять batched-данные с timestamp’ами «из прошлого» и привязкой к `run_id`/`capture_session_id`;
  - корректно обрабатывать out-of-order точки и большие батчи (лимиты/стриминг);
  - зафиксировать доменную политику: допустим ли ingest в `finished/archived`, как маркировать «late data» и как это влияет на отчёты.
  - ⚠️ **MVP реализован:** ingest разрешён для `succeeded/failed` (late data), но запрещён для `archived`:
    - `Run` со статусом `archived` блокирует ingest.
    - `CaptureSession` со статусом `archived` или с флагом `archived=true` блокирует ingest.

### 3. Data Integrity & Scaling (итерации 5‑6)
- **Фоновые задачи (worker):** ❌ Не реализовано. Фоновые задачи для авто-закрытия зависших запусков и реконcиляции capture-сессий, а также мониторинга backfill-процессов отсутствуют. Требуется реализация worker-сервиса.
- **Индексы и денормализации:** ✅ Частично реализовано. Базовые индексы реализованы в миграциях (`experiment-service/migrations/001_initial_schema.sql`): `experiments_project_status_idx`, `experiments_tags_gin_idx`, `experiments_metadata_gin_idx`, `runs_project_status_idx`, `capture_sessions_project_status_idx`. Расширенные индексы для фильтров по времени и активным датчикам могут потребовать оптимизации.
- **TimescaleDB для телеметрии (вариант A: та же БД):** ❌ Не реализовано. План: включить TimescaleDB extension, перевести `telemetry_records` в hypertable по `timestamp` (опционально с partitioning по `sensor_id`), добавить индексируемое поле `signal`, включить retention/compression и (опционально) continuous aggregates. Дизайн зафиксирован в `docs/telemetry-storage-timescaledb.md` и `docs/adr/002-timescaledb-telemetry.md`.
- **Синхронизация по времени между датчиками/группами датчиков:** ❌ Не реализовано. Нужны механизмы для анализа/нормализации временных рядов от разных датчиков:
  - хранить/использовать «время устройства» vs «время приёма» (для оценки clock drift/offset);
  - синхронизировать/выравнивать ряды по группам датчиков (например, по тегу/label на датчике/сессии);
  - определить правила агрегации/интерполяции для отчётов/графиков (ресэмплинг, окно, допуски).
- **Инварианты хранения:** ⚠️ Частично реализовано. Поле `archived` (boolean) существует в таблице `capture_sessions`, но soft delete не реализован на уровне API. Immutable запись историй статусов не реализована.
- **Песочница для нагрузочного тестирования:** ❌ Не реализовано. Песочница для нагрузочного тестирования (pgbench + воспроизведение телеметрии) отсутствует.
- **Контроль версий схем:** ⚠️ Частично реализовано. Миграции SQL существуют, но нет формального changelog и гарантий совместимости назад на 2 релиза. OpenAPI версионирование не реализовано.

### 4. Integrations & Collaboration (итерация 7)
- **Enforcement бизнес-политик:** ❌ Не реализовано. Raw-значения доступны только ролям с разрешением, настройка «physical-only» на уровне проекта не реализована. Требуется реализация политик доступа к данным.
- **Расширенные фильтры API:** ⚠️ Частично реализовано. Базовые фильтры по статусу, проекту, пагинация реализованы. Фильтры по git SHA, участникам, связанным датчикам, версиям профилей преобразования не реализованы.
- **Экспорт данных:** ❌ Не реализовано. Экспорт данных (JSON/CSV) с выбором слоя `raw/physical`, подписка Comparison Service на события обновлений отсутствуют.
- **Подписки на события:** ❌ Не реализовано. Подписки на события через Kafka/Redis Stream для API Gateway и внешних consumers (включая события `conversion_profile.applied`, `capture.backfill.finished`) отсутствуют.
- **Auth Proxy/BFF:** ⚠️ Частично реализовано. Auth Proxy существует в `projects/frontend/apps/auth-proxy/`, реализует единую точку для фронта с куками сессии, проксированием REST, CORS, rate limiting (реализован через `@fastify/rate-limit`), WebSocket поддержка (настроена в `http-proxy`). SSE проксирование и CSRF защита требуют доработки.

### 5. Hardening & Launch (итерация 8+)
- **SLO/SLI мониторинг:** ❌ Не реализовано. SLO/SLI мониторинг (Prometheus): RPS, latency, error rate, очередь задач, доля успешных backfill-операций отсутствует. Требуется интеграция с Prometheus.
- **Трассировка (OpenTelemetry):** ⚠️ Частично реализовано. Структурированное логирование через `structlog` и `backend_common.middleware.trace` реализовано с поддержкой `trace_id` и `request_id`. Полная интеграция OpenTelemetry для критичных сценариев (`create_run`, `close_run`, `capture_start`, `backfill_apply`) не реализована.
- **Chaos-тесты и отказоустойчивость:** ❌ Не реализовано. Chaos-тесты транзакций, failover сценарии БД, реплика + бэкапы, перезапуск backfill без потери идемпотентности отсутствуют.
- **Отказоустойчивость Telemetry Ingest при недоступности БД (disk spool):** ❌ Не реализовано. При проблемах с PostgreSQL/Timescale ingestion не должен терять данные:
  - при ошибках записи в БД — временно складывать батчи на диск (spool) с лимитами по объёму/TTL;
  - реализовать ретраи/экспоненциальный backoff и фоновую доставку из spool при восстановлении БД;
  - определить семантику доставки: best-effort vs at-least-once (и требования к дедупликации/идемпотентности на уровне точек/батча).
- **Документация:** ⚠️ Частично реализовано. Базовая документация существует (README, OpenAPI спецификация). Operational runbook, диаграммы взаимодействий (Telemetry Ingest, Conversion Profiles, Metrics), RFC на дальнейшие фичи требуют доработки.

## Зависимости и интерфейсы
- **Telemetry Ingest:** события начала/окончания capture-сессий, связывание датчиков с запусками, применение профилей конвертации и результаты backfill.
- **Metrics Service:** запросы на агрегации для карточек запусков, подписки на обновления.
- **Artifact Service:** webhooks о смене статуса артефактов, ссылки на approved модели.
- **API Gateway:** агрегированная выдача для внешних клиентов, rate limiting и аутентификация.
- **Auth Proxy/BFF:** хранение access/refresh в HttpOnly куках, валидация сессии для фронта, проксирование `/api/*` и WS/SSE к Experiment Service (и далее к Gateway), нормализация ошибок, CORS/CSRF.
