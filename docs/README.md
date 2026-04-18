# Docs Index

Единый индекс документации репозитория.

## Быстрый старт и окружение
- `setup-guide.md` — установка и настройка окружения разработки.
- `local-dev-docker-setup.md` — локальная разработка через Docker/Docker Compose.
- `local-dev-env-normalization.md` — нормализация локального окружения (версии, переменные, соглашения).
- `local-dev-inventory.md` — что именно крутится локально и как это устроено.

## Процессы разработки
- `pr-guidelines.md` — правила PR (как оформлять, что проверять).
- `code-style-guide.md` — стиль кода и инструменты качества.
- `grading-system.md` — система оценивания (для учебной части проекта).

## Демонстрация
- `demo-flow.md` — сценарий демо (что показать и в каком порядке).
- `manual-testing.md` — ручной E2E чеклист (Portal → Auth Proxy → Services → DB).

## Дебаг и наблюдаемость
- `ui-debugging.md` — debug toast в Portal + как искать `trace_id/request_id` в логах.
- `logging-flow.md` — как логи попадают из сервисов в Grafana/Loki.
- `grafana-trace-filtering.md` — как фильтровать по `trace_id/request_id`.
- `alloy-logs-viewing.md` — быстрые подсказки по Alloy/Loki.

## Спецификации / ТЗ
- `experiment-tracking-ts.md` — ТЗ на платформу Experiment Tracking.
- `telemetry-cli-ts.md` — ТЗ на `telemetry-cli` (агент сбора/отправки телеметрии).
- `telemetry-rc-stm32.md` — заметки/спека по RC + STM32 телеметрии (железо/интеграция).

## Схемы и данные
- `experiment-service-db-schema.md` — схема БД Experiment Service.
- `telemetry-storage-timescaledb.md` — хранение телеметрии датчиков в TimescaleDB (hypertable, индексы, retention/compression).

## Баги
- `bugs.md` — единый список всех известных багов (Frontend + Firmware).

## Roadmap
- `experiment-tracking-status-and-roadmap.md` — единый статус и roadmap платформы (включая Experiment Service и Frontend).

## Архитектурные решения (ADR)
- `adr/` — журнал архитектурных решений (Architecture Decision Records).
- `adr/001-auth-proxy-fastify.md` — Auth Proxy / BFF на Fastify.
- `adr/002-timescaledb-telemetry.md` — TimescaleDB для хранения телеметрии датчиков.


