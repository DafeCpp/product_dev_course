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

## Дорожная карта

### 1. Foundation (итерации 1‑2)
- Доменные модели `Experiment`, `Run`, `CaptureSession`, базовые CRUD-ручки.
- Валидация состояний (`draft → running → finished/failed/archived`), idempotency для повторных запросов.
- Миграции (Alembic) + сиды для тестовых данных.
- OpenAPI v1, генерация client SDK (internal).

### 2. Runs & Capture Management (итерации 3‑4)
- Массовые операции: batch-update статусов запусков, bulk tagging.
- Сущность `CaptureSession`, жизненный цикл «start/stop», связь с Telemetry Ingest по `capture_session_id`.
- Webhook-триггеры `run.started`, `run.finished`, `capture.started`.
- Аудит-лог действий пользователей (create/update/delete).

### 3. Data Integrity & Scaling (итерации 5‑6)
- Фоновые задачи (worker) для авто-закрытия зависших запусков и реконcиляции capture-сессий.
- Индексы и денормализации для быстрых фильтров (по тегам, времени, статусу).
- Песочница для нагрузочного тестирования (pgbench + воспроизведение телеметрии).
- Контроль версий схем (DB + OpenAPI) с changelog и совместимостью назад на 2 релиза.

### 4. Integrations & Collaboration (итерация 7)
- Гранулярные разрешения: уровни доступа `owner/editor/viewer` на сущности Experiment/Run.
- Расширенные фильтры API (по git SHA, участникам, связанным датчикам).
- Экспорт данных (JSON/CSV) и подписка Comparison Service на события обновлений.
- Подписки на события через Kafka/Redis Stream для API Gateway и внешних consumers.

### 5. Hardening & Launch (итерация 8+)
- SLO/SLI мониторинг (Prometheus): RPS, latency, error rate, очередь задач.
- Трассировка (OpenTelemetry) для критичных сценариев `create_run`, `close_run`.
- Chaos-тесты транзакций, failover сценарии БД, реплика + бэкапы.
- Документация: operational runbook, диаграммы взаимодействий, RFC на дальнейшие фичи.

## Зависимости и интерфейсы
- **Telemetry Ingest:** события начала/окончания capture-сессий, связывание датчиков с запусками.
- **Metrics Service:** запросы на агрегации для карточек запусков, подписки на обновления.
- **Artifact Service:** webhooks о смене статуса артефактов, ссылки на approved модели.
- **API Gateway:** агрегированная выдача для внешних клиентов, rate limiting и аутентификация.
