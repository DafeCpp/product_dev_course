# Experiment Service (vNext Skeleton)

Новый каркас сервиса построен вокруг требований из `docs/experiment-tracking-ts.md` и дорожной карты `docs/experiment-service-roadmap.md`. Он ещё не реализует бизнес-логику, но задаёт архитектурные контуры, чтобы команда могла итеративно добавлять функциональность, не нарушая спецификацию.

## Что входит
- aiohttp приложение (`src/experiment_service/main.py`).
- Модульная структура по доменам (experiments, runs, capture sessions, sensors, metrics, artifacts, conversion profiles).
- Pydantic-модели домена со статусами, указанными в ТЗ.
- Заготовки REST-роутов с описанием ожидаемых контрактов (пока возвращают `501`).
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


