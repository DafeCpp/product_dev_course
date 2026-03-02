# Experiment Tracking Platform — статус и roadmap

Этот документ — **единый source of truth** по текущему прогрессу и планам развития платформы.

Он объединяет (и заменяет) два прежних документа:
- `projects/frontend/MVP_STATUS.md` (статус фронтенда для MVP)
- `docs/experiment-service-roadmap.md` (roadmap Experiment Service)

## Быстрые ссылки

- **ТЗ / требования**: `docs/experiment-tracking-ts.md`
- **Acceptance checklist**: `docs/mvp-acceptance-checklist.md`
- **Experiment Service DB schema**: `docs/experiment-service-db-schema.md`
- **ADR: TimescaleDB**: `docs/adr/002-timescaledb-telemetry.md`

---

## Frontend (Experiment Portal) — статус для MVP

### Что уже сделано ✅

#### 1) Базовая структура приложения
- ✅ React + TypeScript + Vite настроен
- ✅ React Router для навигации
- ✅ React Query для работы с API
- ✅ Axios клиент с interceptors для токенов
- ✅ Базовый Layout компонент

#### 2) CRUD экспериментов (Experiments)
- ✅ **Список экспериментов** (`/experiments`)
  - Фильтрация по project_id, status
  - Поиск по названию/описанию
  - Пагинация
  - Отображение статусов, тегов, метаданных
- ✅ **Создание эксперимента** (`/experiments/new`)
  - Форма с полями: project_id, name, description, experiment_type, tags, metadata
  - Валидация JSON для metadata
- ✅ **Детали эксперимента** (`/experiments/:id`)
  - Просмотр всей информации
- Редактирование (✅ реализовано: модальное окно `ExperimentEditModal`, PATCH + смена статуса/архивация)
- ✅ **Удаление эксперимента** (кнопка в деталях, `experimentsApi.delete`)
  - Список запусков эксперимента

#### 3) CRUD запусков (Runs)
- ✅ **Список запусков** (компонент `RunsList`)
  - Отображение в таблице
  - Статусы, параметры, длительность
- ✅ **Детали запуска** (`/runs/:id`)
  - Просмотр информации о запуске
  - Завершение запуска (complete)
  - Пометить как ошибка (fail)
  - Отображение параметров и метаданных
  - Управление capture sessions (старт/стоп отсчёта)
  - Отображение активной capture session
  - Список всех capture sessions

#### 4) API клиент
- ✅ Базовый API клиент (`projects/frontend/apps/experiment-portal/src/api/client.ts`)
- ✅ Методы для experiments: list, get, create, update, delete, search
- ✅ Методы для runs: list, get, create, update, complete, fail
- ✅ Методы для sensors: list, get, create, update, delete, rotateToken
- ✅ Методы для capture sessions: list, create, stop, delete
- ✅ Методы для webhooks и audit: webhooks (list, create, delete), deliveries (list, retry), run_events (list по run_id / session_id)
- ✅ Auth API клиент (`projects/frontend/apps/experiment-portal/src/api/auth.ts`) для работы с Auth Proxy
- ✅ Интеграция с Auth Proxy через HttpOnly cookies
- ✅ Автоматическое обновление токенов через interceptor
- ✅ Обработка 401 ошибок (автоматический refresh)

#### 5) Типы TypeScript
- ✅ Полный набор типов в `projects/frontend/apps/experiment-portal/src/types/index.ts`

#### 6) Телеметрия (SSE viewer)
- ✅ Просмотр live-стрима (SSE) в `TelemetryStreamModal` (с debug toast для ошибок)
- ✅ Страница `/telemetry`: мульти‑панельный просмотр графиков (`TelemetryViewer` + `TelemetryPanel`, Plotly; настройки/панели сохраняются в localStorage)

---

### Что нужно сделать (не MVP / backlog) ❌

#### Улучшения UI/UX
- **Loading states:** ✅ Улучшено: компонент `Loading` со спиннером и текстом; полноэкранная загрузка на списках экспериментов и датчиков (в т.ч. «Загрузка проектов...» до появления выбора); формы/модалки уже имели индикацию при submit (кнопка «Сохранение...», «Вход...» и т.д.).
- **Toast для успешных операций:** ✅ Расширено: тосты успеха уже были на создание/обновление/удаление экспериментов, запусков, датчиков, проектов, webhooks и т.д.; добавлен тост при успешном входе (Login).
- **Валидация форм на клиенте:** ✅ Базовая: обязательные поля (проект, название) и проверка JSON в формах экспериментов и запусков; metadata/params должны быть объектами (не массив), сообщения об ошибках через notifyError.
- **Телеметрия — управление capture_session:** ✅ Реализовано: на странице `/telemetry` при выбранном пуске (run) в блоке фильтров отображаются кнопки «Старт отсчёта» / «Остановить отсчёт» (для run в статусе draft/running); запросы run + experiment и список сессий загружаются при выборе пуска.
- **Просмотр исторических данных для пуска/отсчёта:** ✅ Реализовано: режим «history» в TelemetryViewer — выбор capture session, загрузка точек с пагинацией, Plotly-графики по сенсорам, фильтр сенсоров, raw/physical, include late data, лимит точек, asc/desc.

#### Мобильный клиент (Android)
- Сделать приложение для Android для просмотра результатов (Experiments/Runs) и телеметрии (live + historical)

---

## Cross-cutting backlog (Frontend + BFF/Auth Proxy)

### 🔥 Нормальный вывод ошибок для дебага (toast снизу справа) — HIGH PRIORITY ✅

Цель: при ошибках показывать **детали запроса/ответа** и **trace-id/request-id**, чтобы быстро сопоставлять UI ↔ логи/трейсы.

- **UI:** всплывающее окно снизу справа (toast) с возможностью раскрыть “Details” и кнопкой копирования
- **Запрос (sanitized):** method + URL, query params, основные headers (без секретов), body (если есть; с маскированием токенов/паролей)
- **Ответ:** HTTP status + сырое тело ответа сервиса (если есть)
- **Корреляция:** `trace-id` / `request-id` (например, из `traceparent`, `x-trace-id`, `x-request-id`)

Почему это приоритет:
- снижает время диагностики “почему сломалось” без похода в DevTools/логи
- помогает сразу связать UI-ошибку с backend traces/logs через `trace-id`/`request-id`

Критерии готовности (минимум):
- toast появляется на **все** ошибки запросов (включая network/CORS/timeout)
- есть кнопка **Copy** (копирует единый текст-блок с request/response/correlation)
- секреты маскируются (tokens/passwords/cookies)

✅ **Реализовано** в `experiment-portal`: один тост снизу справа на каждую ошибку (axios + auth client); компонент `DebugErrorToast` с кнопками Details/Copy, санитизация headers/body в `utils/httpDebug.ts`; корреляция из `x-trace-id`, `x-request-id`, `traceparent`.

### SSE proxy + CSRF hardening (Auth Proxy) ✅

✅ Реализовано в `projects/frontend/apps/auth-proxy`:
- ✅ корректное SSE-проксирование (SSE hardening + заголовки `text/event-stream`, `X-Accel-Buffering: no`, `Cache-Control: no-cache`)
- ✅ CSRF защита (double-submit cookie `csrf_token` + проверка `Origin/Referer` + `X-CSRF-Token`; покрыто тестами)

---

## Auth Service — управление пользователями и доступом

### Текущее состояние

- ✅ **Регистрация и логин:** `POST /auth/register` (открытая), `POST /auth/login`, JWT (access + refresh токены), bcrypt.
- ✅ **Смена пароля** (с подтверждением старого): `POST /auth/change-password`.
- ✅ **Проекты и роли:** CRUD проектов, `project_members` с ролями `owner` / `editor` / `viewer`.
- ⚠️ **Начальный admin:** hardcode в миграции `001_initial_schema.sql` (логин `admin`, пароль `admin123`, `password_change_required = true`) — не подходит для продакшена.
- ❌ **Logout / отзыв токенов:** заглушка, refresh-токены не инвалидируются.
- ❌ **Восстановление пароля** и сброс пользователя.
- ❌ **Инвайт-система:** регистрация открытая для всех.
- ❌ **Управление пользователями** admin'ом (список, деактивация, принудительный сброс пароля).

---

### Backlog

#### Bootstrap admin-пользователя при чистой установке

**Проблема:** при деплое без данных нет ни одного пользователя. Хардкод-пароль в миграции
небезопасен и требует ручного исправления.

**Решение:**
- Убрать хардкод из миграции. `001_initial_schema.sql` создаёт только схему без данных.
- Добавить env-переменную `ADMIN_BOOTSTRAP_SECRET` (генерируется оператором при деплое).
- Эндпоинт `POST /auth/admin/bootstrap` (открытый, одноразовый):
  - Принимает `{ "secret": "...", "username": "admin", "password": "..." }`.
  - Проверяет `secret == ADMIN_BOOTSTRAP_SECRET` и что пользователей ещё нет.
  - Создаёт первого пользователя с флагом `is_superadmin = true`.
  - После создания эндпоинт перестаёт работать (идемпотентно).
- Альтернатива: CLI-команда `auth-service bootstrap-admin --secret $SECRET`.

#### Сброс пароля пользователя

**Случаи использования:**
- Пользователь забыл пароль и нет email-восстановления.
- Admin хочет принудительно сбросить пароль скомпрометированного аккаунта.

**Решение:**
- `POST /auth/admin/users/{user_id}/reset-password` (только superadmin):
  - Генерирует временный пароль или принимает новый пароль.
  - Устанавливает `password_change_required = true`.
- Опционально: `POST /auth/forgot-password` → email со ссылкой (требует SMTP-интеграции).

#### Инвайт-система: закрытая регистрация

**Цель:** закрыть открытую регистрацию — новый пользователь может зарегистрироваться только
по ссылке, выданной admin'ом.

**Решение:**
- Таблица `invite_tokens`: `id`, `token` (uuid), `created_by` (FK users), `email_hint`
  (опционально), `expires_at`, `used_at`, `used_by` (FK users).
- `POST /auth/admin/invites` (только superadmin/admin) — создать инвайт, вернуть ссылку
  `{base_url}/register?invite={token}`.
- `GET /auth/admin/invites` — список инвайтов (с фильтрами: активные / использованные).
- `DELETE /auth/admin/invites/{token}` — отозвать инвайт.
- `POST /auth/register` — принимает обязательный `invite_token`; валидирует, помечает
  `used_at`, создаёт пользователя.
- Конфигурационный флаг `REGISTRATION_MODE=open|invite` для обратной совместимости
  в dev-окружении (по умолчанию `invite` в продакшене).

#### Управление пользователями (admin UI)

- `GET /auth/admin/users` — список всех пользователей (только superadmin).
- `PATCH /auth/admin/users/{user_id}` — изменить статус (активен / деактивирован).
- `DELETE /auth/admin/users/{user_id}` — удалить пользователя (с проверкой на активные ресурсы).
- Frontend: страница `/admin/users` с таблицей пользователей, кнопками инвайта и сброса пароля.

#### Опционально: отдельный сервис управления доступами

Если платформа будет масштабироваться (несколько организаций, SSO, LDAP), стоит рассмотреть
выделение auth-service в полноценный Identity-сервис или интеграцию с Keycloak / Ory Hydra.
На текущем этапе достаточно описанных выше эндпоинтов в `auth-service`.

---

## Experiment Service — roadmap

Документ описывает независимый план развития Experiment Service в рамках платформы Experiment Tracking.
Фокус — на возможностях самого сервиса, API-контрактах и взаимодействии с соседними компонентами.

### Цели и принципы
- **Единый источник правды** для сущностей `Experiment`, `Run`, `CaptureSession`.
- **Прозрачные статусы** запусков с историей изменений и массовыми операциями.
- **Гибкая интеграция** с Telemetry Ingest, Metrics и Artifact сервисами через стабильные API.
- **Наблюдаемость и аудит**: трассировки, бизнес-метрики, события для внешних consumers.

### Метрики успеха
- P95 ответа API < 400 мс при 200 RPS.
- MTTR по инцидентам сервиса < 20 мин.
- ≥ 95% критичных операций покрыто интеграционными тестами.
- Zero-downtime миграции для всех новых схем.

### Текущее состояние (актуализируется)
- **✅ Завершено (Foundation):** блок Foundation полностью (миграции, CRUD для `Experiment/Run/CaptureSession`, idempotency, пагинация, OpenAPI, RBAC). Добавлены домены `Sensor` и `ConversionProfile`, статусные машины и покрытие тестами (`tests/test_api_*`). Множественные проекты для датчиков реализованы полностью (backend: таблица `sensor_projects` в миграции `001_initial_schema.sql`, API endpoints, тесты; frontend: UI для управления проектами в `SensorDetail.tsx`). UI для управления доступом к проектам реализован (`ProjectMembersModal` с тестами). Профиль пользователя реализован (`UserProfileModal` с тестами).
- **✅ Завершено (Runs & Capture Management):** batch-update статусов, `CaptureSession` (ordinal_number + статусы), bulk tagging, audit-log (Run/CaptureSession), webhooks (outbox + dispatcher), **backfill/late-data процесс** (API start/complete, привязка late-записей, UI). Остаётся: расширение доменных инвариантов ⚠️.
- **❌ Не реализовано / в backlog:** Telemetry ingest (WebSocket), бизнес‑политики доступа, SLO/SLI мониторинг, chaos‑тесты, operational документация.
  - Примечание: **REST ingest** реализован отдельным сервисом `telemetry-ingest-service` (`POST /api/v1/telemetry`).
  - Примечание: **SSE stream (MVP)** реализован в `telemetry-ingest-service` (`GET /api/v1/telemetry/stream`) и используется во фронтенде (например, `TelemetryStreamModal`, `/telemetry` через `TelemetryViewer`/`TelemetryPanel`).
  - Примечание: **SSE auth (MVP)**: `telemetry-ingest-service` принимает токен как sensor token, так и user JWT (через auth-proxy); для клиентов без возможности выставить заголовок поддержан query `access_token`.
  - Примечание: **MVP late-data политика**: данные после stop capture session не “приклеиваются” обратно к сессии (помечаются в `meta.__system` как late); ingest запрещён для `archived`.
  - В `experiment-service` публичной ручки `/api/v1/telemetry` больше нет; real-time режимы со стороны `experiment-service` и полноценная интеграция со сценариями backfill/реплея — в backlog.
- **Зависимости:** сервис собран на `aiohttp 3.10`, `asyncpg 0.31`, `pydantic-settings 2.4`, `structlog`, `opentelemetry-sdk`/`opentelemetry-instrumentation-aiohttp-server`; тестируется через `pytest`, `pytest-aiohttp`, `yandex-taxi-testsuite[postgresql]`; кодоген — `openapi-generator-cli 7.17`.

---

## Дорожная карта

### 1. Foundation (итерации 1‑2)
- **Доменные модели и CRUD:** ✅ Реализовано. Доменные модели `Experiment`, `Run`, `CaptureSession`, базовые CRUD-ручки c привязкой к проектам. Реализовано в `experiment-service/api/routes/experiments.py`, `runs.py`, `capture_sessions.py`.
- **Множественные проекты для датчиков:** ✅ Реализовано.
  - ✅ **Backend:** таблица `sensor_projects` в миграции `001_initial_schema.sql`, обновление модели `Sensor`, API endpoints, RBAC, тесты.
  - ✅ **Frontend:** методы в API клиенте и UI в `SensorDetail.tsx` (добавление/удаление проектов).
- **Фильтрация датчиков по доступным проектам:** ✅ Реализовано.
  - ✅ **Backend:** `GET /api/v1/sensors` поддерживает `project_id` (один проект) или без него — тогда experiment-service использует заголовок `X-Project-Ids` (список UUID от auth-proxy) и возвращает датчики по всем доступным проектам пользователя; при прямом вызове без заголовка — fallback на `active_project_id` или пустой список.
  - ✅ **Auth-proxy:** при `GET /api/v1/sensors` без `project_id` в query запрашивает у auth-service список проектов пользователя и передаёт их в заголовке `X-Project-Ids`.
  - ✅ **Frontend:** в `SensorsList.tsx` есть выпадающий список проектов и фильтрация через `project_id`; при выборе «все проекты» (или без выбора проекта) запрос идёт без `project_id` и пользователь видит датчики из всех своих проектов.
- **Множественные проекты при создании датчика:** ✅ Реализовано (multi-select; первый проект основной).
- **Валидация состояний и idempotency:** ✅ Реализовано.
- **RBAC-хуки:** ✅ Реализовано (owner/editor/viewer; project scoping).
- **Многоуровневый доступ к проектам:** ✅ Реализовано (project members + роли).
- **Ограничение создания экспериментов:** ✅ Реализовано (owner/editor).
- **Автозаполнение проекта при создании эксперимента:** ✅ Реализовано.
- **UI для управления доступом к проектам:** ✅ Реализовано (`ProjectMembersModal.tsx`).
- **Модальное окно для просмотра и редактирования проектов:** ✅ Реализовано (`ProjectModal`).
- **Профиль пользователя:** ✅ Реализовано (`UserProfileModal.tsx`).
- **Миграции:** ✅ Реализовано (apply on startup).
- **OpenAPI v1:** ✅ Реализовано.

### 2. Runs & Capture Management (итерации 3‑4)
- **Массовые операции:** ✅ Реализовано (`POST /api/v1/runs:batch-status`).
- **Bulk tagging:** ✅ Реализовано (`POST /api/v1/runs:bulk-tags`).
- **Расширенная сущность CaptureSession:** ✅ Реализовано (ordinal_number, статусы, связь с ingest).
- **Контроль доменных инвариантов:** ⚠️ Частично реализовано.
  - ✅ нельзя финализировать `Run`, если есть активные `CaptureSession`
  - ✅ нельзя удалить `Sensor`, если он участвует в активных `CaptureSession`
  - ✅ нельзя удалить активную `CaptureSession`
- **Webhook-триггеры:** ✅ Реализовано (outbox + background dispatcher + dedupe/retry; best-effort delivery).
- **Frontend:** ✅ UI для управления webhooks (список/создание/удаление подписок, просмотр delivery log с фильтрацией по статусу, ручной retry). Реализовано: страница `/webhooks` (`Webhooks.tsx`), навигация в sidebar.
- **Аудит-лог действий пользователей:** ✅ Реализовано для `Run`/`CaptureSession` (events + API для чтения).
- **Frontend:** ✅ UI для просмотра audit-log: компонент `AuditLog` (timeline с типами событий, actor, payload; пагинация; collapsible). Интегрирован в `RunDetail` (события запуска + события каждой capture session).
- **Догрузка данных после завершения (late/backfill ingest):** ✅ Реализовано.
  - ✅ **Backfill API:** `POST /api/v1/runs/{run_id}/capture-sessions/{session_id}/backfill/start` — переводит `succeeded` → `backfilling`; `POST .../backfill/complete` — привязывает late-записи (UPDATE `telemetry_records.capture_session_id` по `meta.__system.capture_session_id`) и возвращает `backfilling` → `succeeded`; ответ содержит `attached_records`.
  - ✅ **Audit + Webhooks:** события `capture_session.backfill_started` / `capture_session.backfill_completed` записываются в audit-log и диспатчатся через webhooks.
  - ✅ **Frontend:** кнопки «Начать догрузку» / «Завершить догрузку» в `TelemetryViewer` (history mode) рядом с выбором capture session.
  - ✅ **MVP-политика сохранена:** ingest разрешён для `succeeded/failed` (late data), запрещён для `archived`; во время backfilling данные привязываются как обычные (не late).

### 3. Data Integrity & Scaling (итерации 5‑6)
- **Фоновые задачи (worker):** ✅ Реализовано: in-process async worker (`background_tasks.py`), запускается вместе с aiohttp-сервером, выполняет периодический sweep (интервал `worker_interval_seconds`, по умолчанию 60 с):
  - ✅ Очистка idempotency-ключей старше `idempotency_ttl_hours` (48 ч)
  - ✅ Автостоп зависших capture sessions (`running`/`backfilling` дольше `stale_session_max_hours` — 24 ч) → `failed`
  - ✅ Восстановление застрявших webhook deliveries (`in_progress` дольше `webhook_stuck_minutes` — 10 мин) → `pending`
  - ✅ Очистка старых succeeded webhook deliveries (старше `webhook_succeeded_retention_days` — 30 дней)
- **Индексы и денормализации:** ✅ Частично (базовые индексы/GIN есть, но без целевых денормализаций и оптимизации под нагрузку)
- **TimescaleDB для телеметрии:** ✅ Реализовано (см. `docs/adr/002-timescaledb-telemetry.md`).
  - ✅ **Hypertable:** `telemetry_records` — hypertable с time partitioning по `timestamp` и space partitioning по `sensor_id` (8 партиций, chunk interval 1 день).
  - ✅ **Generated column `signal`:** extracted from `meta->>'signal'` для индексирования; индекс `telemetry_records_sensor_signal_ts_idx`.
  - ✅ **Compression:** `compress_segmentby = 'sensor_id, signal'`, `compress_orderby = 'timestamp DESC'`; автоматическое сжатие чанков старше 7 дней.
  - ✅ **Retention:** автоматическое удаление сырых точек старше 90 дней.
  - ✅ **Continuous aggregate `telemetry_1m`:** 1-минутный downsampling (avg/min/max по raw_value и physical_value, sample_count), группировка по `sensor_id`, `signal`, `capture_session_id`; refresh policy (start_offset 7d, end_offset 1m, каждую минуту). Миграция `002_continuous_aggregates.sql`.
  - ✅ **API:** `GET /api/v1/telemetry/aggregated` в telemetry-ingest-service — запрос из `telemetry_1m` с фильтрами `capture_session_id`, `sensor_id`, `signal`, `time_from`/`time_to`, `limit`, `order`.
  - ✅ **Frontend:** чекбокс «агрегация 1m» в TelemetryViewer (history mode); при включении данные загружаются из continuous aggregate и отображаются как avg-линия с min/max band (Plotly fill).
- **Синхронизация по времени между датчиками:** ❌
- **Инварианты хранения:** ⚠️ Частично (есть DB-констрейнты/уникальности/ссылочная целостность, но нет политик retention/дедупликации/временной синхронизации)
- **Песочница для нагрузочного тестирования:** ❌
- **Контроль версий схем:** ✅ Реализовано (таблица `schema_migrations` + checksum, применение миграций на старте и через `bin/migrate.py`)

### 3.5 Схемы преобразования датчиков (Conversion Profiles)

> **Статус:** ✅ Реализовано (этапы 1–3).
> **Приоритет:** Высокий — ключевая фича ТЗ (раздел 6.2).

#### Что реализовано

- ✅ **DB-схема:** таблица `conversion_profiles`, FK `sensors.active_profile_id`, FK `telemetry_records.conversion_profile_id`, enum `telemetry_conversion_status`. Таблица `conversion_backfill_tasks` для фоновых задач пересчёта (миграция `003_conversion_backfill.sql`).
- ✅ **Backend (experiment-service):**
  - Repository: `ConversionProfileRepository` — CRUD, `publish_profile()` (atomic).
  - Service: `ConversionProfileService` — list, create, publish.
  - `BackfillTaskRepository` + `BackfillService` — создание/получение задач пересчёта.
  - API routes: профили (`POST/GET/publish`), backfill (`POST/GET /api/v1/sensors/{sensor_id}/backfill`).
  - `TelemetryService._apply_conversion()` делегирует в `backend_common.conversion.apply_conversion()`.
  - Background worker `conversion_backfill` — обрабатывает pending задачи батчами по 1000 записей.
- ✅ **Shared conversion module** (`backend_common/conversion.py`): три вида конверсии — `linear` (a·x+b), `polynomial` (Σcᵢ·xⁱ), `lookup_table` (линейная интерполяция + clamp).
- ✅ **Telemetry ingest (real-time):**
  - `ProfileCache` в telemetry-ingest-service — in-memory TTL-кэш (60 сек) для активных профилей.
  - `_bulk_insert()` автоматически применяет конверсию: `raw_only` → `converted` / `conversion_failed`.
  - Приоритет клиента: `physical_value` от клиента → `client_provided`.
- ✅ **Frontend:**
  - Типы: `ConversionProfile`, `ConversionProfilesListResponse`, `BackfillTask`, `BackfillTasksListResponse`.
  - API: `conversionProfilesApi` (list, create, publish), `backfillApi` (start, list, get).
  - `ConversionProfileCreateModal` — создание профиля с выбором kind, специализированными полями для каждого типа, live-калькулятор (предпросмотр).
  - `SensorDetail` — секция «Профили преобразования» (таблица, создание, публикация) + секция «Пересчёт данных» (запуск backfill, progress bar, автообновление).

#### Что нужно реализовать

##### Этап 1: Серверное преобразование при ingest (real-time)

**Цель:** при получении `raw_value` без `physical_value` telemetry-ingest-service автоматически применяет активный профиль датчика.

1. **Загрузка и кэширование профилей** в telemetry-ingest-service:
   - При старте/первом использовании: `SELECT cp.* FROM conversion_profiles cp JOIN sensors s ON s.active_profile_id = cp.id WHERE s.id = $1`.
   - In-memory кэш `{sensor_id → ConversionProfile}` с TTL (например, 60 сек) или инвалидацией через webhook/event при публикации нового профиля.
   - Fallback: если профиля нет → оставить `physical_value = NULL`, `conversion_status = 'raw_only'`.

2. **Применение преобразования** (в `_bulk_insert` перед INSERT):
   - Если `reading.physical_value IS NOT NULL` → `client_provided` (приоритет клиента).
   - Иначе если `active_profile` для этого `sensor_id`:
     - `kind = 'linear'`: `physical = payload.a * raw + payload.b`.
     - `kind = 'polynomial'`: `physical = Σ(payload.coefficients[i] * raw^i)`.
     - `kind = 'lookup_table'`: линейная интерполяция по `payload.table` (`[{raw, physical}, ...]`).
   - При успехе: `conversion_status = 'converted'`, `conversion_profile_id = profile.id`.
   - При ошибке: `conversion_status = 'conversion_failed'`, `physical_value = NULL`.

3. **Поддерживаемые виды (`kind`)**:

| Kind | Payload (JSONB) | Формула |
|------|----------------|---------|
| `linear` | `{"a": float, "b": float}` | `physical = a * raw + b` |
| `polynomial` | `{"coefficients": [c0, c1, c2, ...]}` | `physical = c0 + c1*x + c2*x² + ...` |
| `lookup_table` | `{"table": [{"raw": r, "physical": p}, ...]}` | Линейная интерполяция; экстраполяция за границами — clamp к крайним значениям |

4. **Невалидный payload** → `conversion_failed` + лог с деталями.

##### Этап 2: Frontend — управление профилями

1. **API client**: `conversionProfilesApi.list(sensorId)`, `.create(sensorId, data)`, `.publish(sensorId, profileId)`.
2. **Sensor Detail** — секция «Профили преобразования»:
   - Таблица: version, kind, status, valid_from/to, created_by, published_by.
   - Бейдж активного профиля.
   - Кнопка «Создать профиль» → модалка:
     - Выбор kind (linear / polynomial / lookup_table).
     - Редактор payload (JSON или специализированные поля для каждого kind).
     - Предпросмотр: ввести raw → показать physical (live-калькулятор).
   - Кнопка «Опубликовать» (owner only) → POST `.../publish`.
3. **CreateSensor** — добавить optional секцию «Начальный профиль преобразования» (kind + payload).

##### Этап 3: Backfill engine (пересчёт при смене профиля)

**Цель:** при публикации нового профиля пользователь может запустить пересчёт `physical_value` для исторических данных.

1. **API:**
   - `POST /api/v1/sensors/{sensor_id}/conversion-profiles/{profile_id}/backfill` — запускает пересчёт.
   - Body: `{"scope": "all" | "capture_sessions", "capture_session_ids": [...], "time_from": "...", "time_to": "..."}`.
   - Возвращает `backfill_task_id`.

2. **Хранение задач:**
   - Таблица `conversion_backfill_tasks` (id, sensor_id, profile_id, scope, status [pending/running/succeeded/failed], total_records, processed_records, error, created_at, finished_at).
   - Статус: `pending → running → succeeded | failed`.

3. **Выполнение:**
   - Background worker task: `conversion_backfill_worker`.
   - Читает `telemetry_records` батчами (ORDER BY timestamp ASC, LIMIT 1000).
   - Для каждой записи: загружает профиль, применяет конверсию, UPDATE `physical_value`, `conversion_profile_id`, `conversion_status`.
   - Сохраняет прогресс (processed_records) в таблицу задач.
   - **Идемпотентность:** повторный запуск продолжает с последней обработанной записи.
   - **Сохранение аудита:** старая `conversion_profile_id` логируется (или хранится в `meta.__system.prev_profile_id`).

4. **Frontend:**
   - После publish → предложить «Пересчитать historical данные?».
   - Диалог выбора scope (все данные / конкретные capture sessions).
   - Прогресс-бар (poll GET `/backfill-tasks/{id}`).

5. **Webhook/audit events:** `conversion_profile.backfill_started`, `conversion_profile.backfill_completed`.

##### Этап 4: Расширения (backlog)

- **Scheduled profiles:** автоматическая активация по `valid_from` (worker task проверяет `status = 'scheduled'` с `valid_from <= now()`).
- **Custom formula:** пользовательское Python-выражение в sandbox (eval ограничен whitelist операций, без imports).
- **A/B профили:** два active профиля с split по run_id / capture_session_id для сравнения формул.
- **Валидация при создании:** проверка payload schema по kind (a/b для linear, coefficients для polynomial, table format для lookup).
- **Batch ingest с конверсией:** при batch insert > 100 readings — параллельная конверсия с пулом.

#### Зависимости

- Этап 1 зависит от: существующих API профилей в experiment-service (✅), загрузки профиля в telemetry-ingest-service.
- Этап 2 зависит от: существующих API (✅), нового API-клиента на фронте.
- Этап 3 зависит от: worker-инфраструктуры (✅ `BackgroundWorker`), новой таблицы `conversion_backfill_tasks`.

#### Критерии готовности (минимум для «реализовано»)

- [x] Ingest с `raw_value` без `physical_value` → сервер применяет active profile (linear/polynomial/lookup_table) → `physical_value` != NULL, `conversion_status = 'converted'`.
- [x] Frontend: список профилей в Sensor Detail, создание профиля (linear/polynomial/lookup_table), live-предпросмотр, публикация.
- [x] Backfill: запуск пересчёта через API, background worker обрабатывает батчами, прогресс виден в UI (progress bar, auto-refresh).
- [x] Тесты: unit-тесты для `backend_common.conversion` (linear, polynomial, lookup_table, edge cases).

### 4. Integrations & Collaboration (итерация 7)
- **Enforcement бизнес-политик:** ❌
- **Расширенные фильтры API:** ✅ Реализовано: `GET /api/v1/experiments` поддерживает `?status=`, `?tags=` (comma-separated, @> containment), `?created_after=`, `?created_before=` (ISO-8601); `GET /api/v1/experiments/{id}/runs` — те же фильтры; `GET /api/v1/sensors` — `?status=`, `?created_after=`, `?created_before=`; поиск по тексту — `GET /api/v1/experiments/search?q=`.
- **Экспорт данных:** ✅ Частично реализовано.
  - ✅ **Метаданные:** `GET /api/v1/experiments/export?format=csv|json` и `GET /api/v1/experiments/{id}/runs/export?format=csv|json` с поддержкой фильтров (status, tags, created_after, created_before); до 5000 записей; на фронтенде кнопки «CSV» / «JSON» на списке экспериментов и списке запусков.
  - ✅ **Экспорт с данными датчиков (телеметрия) — Этап 1+2:** Backend API + Frontend UI для экспорта telemetry readings из capture sessions. Подробный план и статус ниже.
- **Подписки на события:** ❌

### 4.5 Экспорт данных с телеметрией датчиков

> **Статус:** ✅ Этапы 1 и 2 реализованы (backend API + frontend UI). Этап 3 (расширенный экспорт) — backlog.
> **Приоритет:** Средний — важно для анализа данных вне платформы (Jupyter, MATLAB, Excel).

#### Текущее состояние

Реализованы два API-эндпоинта для экспорта телеметрии и кнопки экспорта в RunDetail.

#### Что нужно реализовать

##### Этап 1: Backend — API экспорта телеметрии ✅

**Цель:** API для выгрузки readings привязанных к конкретным capture sessions / runs.

**Реализовано в** `experiment_service/api/routes/telemetry_export.py`.

1. **Экспорт по capture session:** ✅
   - `GET /api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export`
   - Query params: `format=csv|json`, `sensor_id` (опционально, фильтр), `signal` (опционально), `include_late=true|false`, `raw_or_physical=raw|physical|both`.
   - CSV-формат: `timestamp, sensor_id, signal, raw_value, physical_value, conversion_status, capture_session_id`.
   - TODO: стриминговая отдача (chunked transfer) для больших выгрузок (текущая реализация — in-memory, лимит 100 000 записей).

2. **Экспорт по run (все capture sessions):** ✅
   - `GET /api/v1/runs/{run_id}/telemetry/export`
   - Аналогичные query params + `capture_session_id` (опционально, фильтр).
   - CSV добавляет колонку `capture_session_id`.

3. **Агрегированный экспорт:** ✅
   - `GET /api/v1/runs/{run_id}/telemetry/export?aggregation=1m` — использует continuous aggregate `telemetry_1m`.
   - CSV-формат: `bucket, sensor_id, signal, capture_session_id, sample_count, avg_raw, min_raw, max_raw, avg_physical, min_physical, max_physical`.

4. **Ограничения и безопасность:** ✅ (частично)
   - ✅ Лимит записей: 100 000 с header `X-Export-Truncated: true` если превышен.
   - ✅ RBAC: доступ к данным только для участников проекта (через `resolve_project_id` + `ensure_project_access`).
   - ❌ Rate limit: не реализован (backlog).

##### Этап 2: Frontend — UI экспорта телеметрии ✅ (базовый)

1. **RunDetail / capture session list:** ✅
   - Кнопка «Экспорт телеметрии» рядом с каждой capture session → скачать CSV readings этой сессии.
   - Кнопка «Экспорт всей телеметрии» на уровне run → скачать readings всех capture sessions.

2. **Диалог настроек экспорта:** ✅ Реализовано.
   - Компонент `TelemetryExportModal`: формат (CSV/JSON), значения (raw/physical/both), агрегация 1m, include_late, фильтр по датчику и сигналу, фильтр по сессии (для экспорта всего run).
   - Интегрирован в `RunDetail` — кнопки «Экспорт телеметрии…» у каждой сессии и для всего run.

3. **TelemetryViewer (history mode):** ✅ Реализовано.
   - Кнопка «Экспорт данных…» открывает `TelemetryExportModal`, предзаполненный текущим состоянием (сессия, датчик, raw/physical, include_late, агрегация).

##### Этап 3: Расширенный экспорт (backlog)

- **Полный экспорт эксперимента:** одним архивом (ZIP): метаданные эксперимента + все runs + все capture sessions + все telemetry readings. Формат: директория `experiment_<id>/runs/<run_id>/sessions/<session_id>/telemetry.csv`.
- **Формат Parquet:** для интеграции с pandas/PySpark; более компактный чем CSV для больших выгрузок.
- **Асинхронный экспорт:** для больших объёмов (>100k записей) — создание задачи, фоновая генерация файла, уведомление о готовности, ссылка на скачивание.
- **Шаблоны экспорта:** пользователь может сохранить конфигурацию экспорта (фильтры, колонки, формат) и переиспользовать.

#### Зависимости

- Этап 1 зависит от: доступа к `telemetry_records` из experiment-service или inter-service API к telemetry-ingest-service; continuous aggregate `telemetry_1m` (✅).
- Этап 2 зависит от: API этапа 1, текущего UI RunDetail/TelemetryViewer.
- Этап 3 зависит от: worker-инфраструктуры (✅ `BackgroundWorker`), object storage для файлов.

#### Критерии готовности (минимум)

- [x] API: экспорт readings по capture session в CSV с фильтрами sensor_id, signal, raw/physical, include_late, aggregation.
- [x] API: экспорт readings по run (все capture sessions) в CSV/JSON.
- [x] Frontend: кнопка экспорта в RunDetail, скачивание файла.
- [ ] Стриминг: экспорт 50k+ записей без OOM (текущий лимит 100k in-memory, достаточно для MVP).
- [x] Тесты: integration (создать session -> ingest data -> export -> verify CSV).

### 5. Hardening & Launch (итерация 8+)
- **SLO/SLI мониторинг:** ❌
- **Трассировка (OpenTelemetry):** ✅ Реализовано: `TracerProvider` + OTLP HTTP exporter + авто-инструментализация aiohttp-server (`opentelemetry-instrumentation-aiohttp-server`); активируется при наличии `OTEL_EXPORTER_ENDPOINT`; `get_tracer()` доступен для ручных спанов в сервисном/репозиторном коде; graceful shutdown с flush спанов.
- **Chaos-тесты и отказоустойчивость:** ❌
- **Telemetry ingest disk spool:** ❌
- **Документация:** ✅ (индекс `docs/README.md`, quickstart `docs/local-dev-docker-setup.md`, demo `docs/demo-flow.md`, E2E чеклист `docs/manual-testing.md`, дебаг `docs/ui-debugging.md`)

