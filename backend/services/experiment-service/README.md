# Experiment Service

Сервис реализует доменную часть Experiment Tracking Platform и соответствует требованиям из `docs/experiment-tracking-ts.md` и дорожной карте `docs/experiment-service-roadmap.md`. На текущей итерации доступны CRUD-операции для экспериментов, запусков и capture session, управление датчиками и профилями преобразования, а также вспомогательные механизмы (идемпотентность, пагинация, status machine).

## Возможности

- **REST API (aiohttp):**
  - `/api/v1/experiments`, `/runs`, `/capture-sessions` — полные CRUD + статусные переходы, batch-обновления и кнопка `archive`.
  - `/api/v1/sensors`, `/conversion-profiles` — регистрация датчиков, ротация токенов, просмотр и публикация версий профилей.
  - Идемпотентные POST-запросы через заголовок `Idempotency-Key` (повторы → кешированный ответ, конфликт → `409`).
  - Пагинация и валидация UUID/ролей на всех ручках.
- **Доменная модель:** Pydantic DTO/модели с проверкой переходов (см. `services/state_machine.py`) и строгим `extra="forbid"`.
- **Хранилище:** PostgreSQL 15+ с миграциями из `migrations/`, доступ через `asyncpg` и тонкие репозитории (`repositories/*.py`).
- **Безопасность и доступ:** временная интеграция с Auth через дебаг-заголовки `X-User-Id/X-Project-Id/X-Project-Role`; права `owner/editor/viewer` уже проверяются на уровне middleware/dependency.
- **Документация и SDK:** OpenAPI 3.1 в `openapi/openapi.yaml`, конфиги генераторов для TypeScript Fetch и cpprestsdk; `make generate-sdk` обновляет клиентов.
- **Тесты:** интеграционные сценарии на testsuite (см. `tests/test_api_*.py`) покрывают позитивные флоу, ошибки/конфликты, базовый RBAC и заглушки ingest.

## Зависимости

| Категория        | Пакеты / версии                                         | Назначение                        |
|------------------|--------------------------------------------------------|----------------------------------|
| Runtime          | `python 3.11+`, `aiohttp 3.10`, `asyncpg 0.29`, `pydantic-settings 2.4`, `orjson`, `structlog` | HTTP API, PostgreSQL, конфигурация и логирование |
| Dev/Test         | `pytest 8`, `pytest-asyncio`, `pytest-aiohttp`, `yandex-taxi-testsuite[postgresql]`, `ruff`, `mypy` | автотесты, статический анализ    |
| Tooling / SDK    | `openapi-generator-cli 7.17`, `httpx`                   | генерация клиентов и smoke-тесты |

Полный список находится в `pyproject.toml`. Для SDK требуется Java 17+.

## Быстрый старт

```bash
cd backend/services/experiment-service
poetry install
cp .env.example .env
# локальный запуск (ожидает PostgreSQL по settings.database_url)
poetry run python -m experiment_service.main

# тесты + встроенный PostgreSQL через testsuite
poetry run pytest
```

## Работа с миграциями

Все изменения схемы находятся в `migrations/*.sql`. Применение/проверка:

```bash
poetry run python bin/migrate.py --database-url postgres://user:pass@localhost:5432/experiment_service
poetry run python bin/migrate.py --dry-run  # список доступных миграций
```

Файл `tests/schemas/postgresql/experiment_service.sql` генерируется из миграций (`poetry run python bin/export_schema.py`) и используется testsuite для подъёма временной БД.

## Sensors & Conversion Profiles (пример)

```http
POST /api/v1/sensors
Idempotency-Key: sensor-1

{
  "project_id": "11111111-2222-3333-4444-555555555555",
  "name": "thermo-1",
  "type": "thermocouple",
  "input_unit": "mV",
  "display_unit": "C",
  "conversion_profile": {
    "version": "v1",
    "kind": "linear",
    "payload": {"a": 1.4, "b": 0.22}
  }
}
```

Ответ содержит объект датчика и одноразовый токен (`token_preview` хранится в БД, полный токен возвращается только при создании/ротации).

## Генерация SDK

```bash
make generate-sdk  # агрегирует команды openapi-generator-cli
# или вручную:
poetry run openapi-generator-cli generate \
  -i openapi/openapi.yaml \
  -g typescript-fetch \
  -o clients/typescript-fetch \
  -c openapi/clients/typescript-fetch-config.yaml
```

Каталоги `clients/*` очищаются перед генерацией и игнорируются Git.

## Текущее состояние и ограничения

- Завершён блок **Foundation** + большая часть **Runs & Capture Management** из roadmap: репозитории, state machine, batch-операции, аудит событий через таблицы `capture_session_events`.
- **Ещё не реализовано:** ingestion `/api/v1/telemetry`, `/api/v1/runs/{id}/metrics`, артефакты, привязка датчиков к запускам (`run_sensors`), вебхуки и интеграции с Auth Service. В API на эти ручки возвращается `501` либо заглушка WebSocket.
- RBAC пока завязан на заголовки; переход на реальный Auth Service и audit trail планируется следующим этапом (см. ниже).

## Следующие шаги

1. **Telemetry & Metrics ingest:** REST + WebSocket/SSE, хранение raw/physical значений, SLA и heartbeat.
2. **Run↔Sensor binding и артефакты:** использование таблиц `run_sensors`, CRUD/approve для `/artifacts`.
3. **RBAC & Auth integration:** обмен токенами с Auth Service, audit-log на старты/остановки capture session, политики raw/physical.
4. **Наблюдаемость и события:** Prometheus/OTel метрики, вебхуки `run.started/capture.completed`, очереди для API Gateway.
5. **Документация:** синхронизация OpenAPI/AsyncAPI и обновление демо-сценариев `bin/demo_seed.py` (после реализации ingest).

Сервис развивается итеративно; актуальный прогресс фиксируется в `docs/experiment-service-roadmap.md`.

