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
- **Завершено:** блок Foundation полностью (миграции, CRUD для `Experiment/Run/CaptureSession`, idempotency, пагинация, OpenAPI). Добавлены домены `Sensor` и `ConversionProfile`, статусные машины и покрытие тестами (`tests/test_api_*`).
- **В процессе:** этап Runs & Capture Management — реализованы batch-операции, проверки инвариантов, заглушки webhook/ingest, но ещё отсутствуют `run_sensors`, `capture_session_events` в API и артефактный контур.
- **Не реализовано:** Telemetry ingest (REST + WS/SSE), `/runs/{id}/metrics`, артефакты, webhooks/Kafka события, интеграция с Auth Service, прослойка Auth Proxy/BFF. Эти задачи остаются в очереди этапов 2‑4.
- **Зависимости:** сервис собран на `aiohttp 3.10`, `asyncpg 0.29`, `pydantic-settings 2.4`, `structlog`, тестируется через `pytest`, `pytest-aiohttp`, `yandex-taxi-testsuite[postgresql]`, кодоген осуществляется `openapi-generator-cli 7.17`.

## Дорожная карта

### 1. Foundation (итерации 1‑2)
- Доменные модели `Experiment`, `Run`, `CaptureSession`, базовые CRUD-ручки c привязкой к проектам.
- Валидация состояний (`draft → running → finished/failed/archived`), idempotency для повторных запросов.
- RBAC-хуки (owner/editor/viewer) и enforcing project-level scope на всех ручках.
- Миграции (Alembic) + сиды для тестовых данных.
- OpenAPI v1, генерация client SDK (internal).

### 2. Runs & Capture Management (итерации 3‑4)
- Массовые операции: batch-update статусов запусков, bulk tagging.
- Расширенная сущность `CaptureSession`: несколько сессий на один запуск, статусная машина `draft/running/failed/succeeded/archived`, порядковые номера и связь с Telemetry Ingest по `capture_session_id`.
- Контроль доменных инвариантов:
  - запуск нельзя архивировать при активных capture session;
  - датчик нельзя отвязать/удалить, если у него есть активные capture session;
  - удаление capture session возможно только в `draft/failed`.
- Webhook-триггеры `run.started`, `run.finished`, `capture.started`, `capture.completed`.
- Аудит-лог действий пользователей (create/update/delete/start/stop/backfill) с фиксацией роли.

### 3. Data Integrity & Scaling (итерации 5‑6)
- Фоновые задачи (worker) для авто-закрытия зависших запусков и реконcиляции capture-сессий, а также мониторинга backfill-процессов.
- Индексы и денормализации для быстрых фильтров (по тегам, времени, статусу, активным датчикам).
- Инварианты хранения: soft delete для завершённых capture session, immutable запись историй статусов.
- Песочница для нагрузочного тестирования (pgbench + воспроизведение телеметрии).
- Контроль версий схем (DB + OpenAPI) с changelog и совместимостью назад на 2 релиза.

### 4. Integrations & Collaboration (итерация 7)
- Enforcement бизнес-политик: raw-значения доступны только ролям с разрешением, настройка «physical-only» на уровне проекта.
- Расширенные фильтры API (по git SHA, участникам, связанным датчикам, версиям профилей преобразования).
- Экспорт данных (JSON/CSV) с выбором слоя `raw/physical`, подписка Comparison Service на события обновлений.
- Подписки на события через Kafka/Redis Stream для API Gateway и внешних consumers (включая события `conversion_profile.applied`, `capture.backfill.finished`).
- Auth Proxy/BFF: единая точка для фронта с куками сессии, проксированием REST/WS/SSE, CORS/CSRF, rate limiting на сессию и аудитом логинов.

### 5. Hardening & Launch (итерация 8+)
- SLO/SLI мониторинг (Prometheus): RPS, latency, error rate, очередь задач, доля успешных backfill-операций.
- Трассировка (OpenTelemetry) для критичных сценариев `create_run`, `close_run`, `capture_start`, `backfill_apply`.
- Chaos-тесты транзакций, failover сценарии БД, реплика + бэкапы; перезапуск backfill без потери идемпотентности.
- Документация: operational runbook, диаграммы взаимодействий (Telemetry Ingest, Conversion Profiles, Metrics), RFC на дальнейшие фичи.

## Зависимости и интерфейсы
- **Telemetry Ingest:** события начала/окончания capture-сессий, связывание датчиков с запусками, применение профилей конвертации и результаты backfill.
- **Metrics Service:** запросы на агрегации для карточек запусков, подписки на обновления.
- **Artifact Service:** webhooks о смене статуса артефактов, ссылки на approved модели.
- **API Gateway:** агрегированная выдача для внешних клиентов, rate limiting и аутентификация.
- **Auth Proxy/BFF:** хранение access/refresh в HttpOnly куках, валидация сессии для фронта, проксирование `/api/*` и WS/SSE к Experiment Service (и далее к Gateway), нормализация ошибок, CORS/CSRF.
