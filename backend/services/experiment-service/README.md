# Experiment Service (vNext Skeleton)

Новый каркас сервиса построен вокруг требований из `docs/experiment-tracking-ts.md` и дорожной карты `docs/experiment-service-roadmap.md`. Он уже реализует базовый CRUD-слой для `Experiment`, `Run`, `CaptureSession`, а также добавленную в этой итерации поддержку `Sensor` + `ConversionProfile` с проверкой статусов и идемпотентностью на write-операциях.

## Что входит
- aiohttp приложение (`src/experiment_service/main.py`).
- Модульная структура по доменам (experiments, runs, capture sessions, sensors, metrics, artifacts, conversion profiles).
- Pydantic-модели домена со статусами, указанными в ТЗ.
- REST-роуты для `experiments`, `runs`, `capture-sessions`, `sensors` и `conversion-profiles`, включая статусные проверки и идемпотентность на POST.
- Конфигурация через `Settings` (Pydantic Settings) с подготовленными переменными окружения.
- Асинхронный доступ к PostgreSQL через `asyncpg` (без SQLAlchemy).
- Список зависимостей для логирования, тестирования и интеграций собран в `pyproject.toml`.
- OpenAPI 3.1 спецификация лежит в `openapi/openapi.yaml` и доступна по `GET /openapi.yaml`.

## Быстрый старт
```bash
cd backend/services/experiment-service
poetry install
cp .env.example .env
poetry run python -m experiment_service.main
```

## SQL-миграции
- Все изменения схемы описываются в каталоге `migrations/` нумерованными `.sql` файлами (`001_initial_schema.sql` и т.д.).
- Таблица `schema_migrations` хранит список применённых версий и checksum файла.
- Применить (или проверить) миграции можно утилитой `bin/migrate.py`:

```bash
poetry run python bin/migrate.py --database-url postgres://user:pass@localhost:5432/experiment_service
# посмотреть список, не применяя
poetry run python bin/migrate.py --dry-run
```

Тестовый стенд (`tests/schemas/postgresql/experiment_service.sql`) генерируется автоматически из миграций:

```bash
poetry run python bin/export_schema.py
```

Этот файл использует testsuite при создании локального PostgreSQL-инстанса и всегда должен быть синхронизирован с последними миграциями.

## Sensors & Conversion Profiles

- `POST /api/v1/sensors` и `POST /api/v1/sensors/{sensor_id}/conversion-profiles` поддерживают идемпотентность через заголовок `Idempotency-Key`. Повторный запрос с тем же телом вернёт закэшированный ответ, конфликтующие payload'ы получают `409`.
- Регистрация датчика принимает базовые поля (`project_id`, `name`, `type`, единицы измерений) и опциональный блок `conversion_profile`:

```json
{
  "project_id": "11111111-2222-3333-4444-555555555555",
  "name": "thermo-1",
  "type": "thermocouple",
  "input_unit": "mV",
  "display_unit": "C",
  "conversion_profile": {
    "version": "v1",
    "kind": "linear",
    "payload": {"a": 1.4, "b": 0.22},
    "status": "draft"
  }
}
```

- Успешный ответ содержит сам объект и последний сгенерированный токен:

```json
{
  "sensor": { "...": "..." },
  "token": "ZyYv2..."
}
```

- `GET /api/v1/sensors` и `/conversion-profiles` возвращают пагинированный ответ (`sensors[]`, `total`, `page`, `page_size`), совпадающий с `paginated_response` из API-утилит.

OpenAPI (`openapi/openapi.yaml`) синхронизирован с текущими DTO и позволяет генерировать клиент/SDK.

## Следующие шаги
1. **Persisted storage layer:** реализовать репозитории поверх `asyncpg`, покрывающие доменные сценарии и используемые API.
2. **Интеграции:** реализовать ingest `/telemetry`, `/metrics`, webhook-потоки и очереди событий.
3. **RBAC и аутентификация:** заменить заглушки зависимостей на реальный вызов Auth Service + аудит.
4. **Тесты и документация:** добавить unit/integration тесты и сформировать OpenAPI/AsyncAPI контракт.

Скелет поддерживает поэтапное развитие без переезда архитектуры в середине работы. Каждый модуль содержит TODO-комментарии с ссылкой на соответствующий раздел требований.

## Тестирование
В репозитории включён `pytest` + [yandex-taxi-testsuite](https://github.com/yandex/yandex-taxi-testsuite) (ставится из PyPI как `yandex-taxi-testsuite[postgresql]`). Плагин `testsuite.pytest_plugin` подключён в `tests/conftest.py`, а фикстура `service_client` создаёт aiohttp-клиент сервиса. Запуск тестов:

```bash
poetry install
poetry run pytest
```


