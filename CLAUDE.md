# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Учебная платформа (2 семестра, МФТИ) + рабочий Experiment Tracking Platform (аналог MLflow).
Помимо бэкенда, есть прошивка для RC-машинки на ESP32-S3.

---

## Архитектура

```
projects/
├── backend/
│   ├── common/                     # Shared Python utilities
│   └── services/
│       ├── auth-service/           # JWT-аутентификация, порт 8001
│       ├── experiment-service/     # Ядро платформы, порт 8002
│       └── telemetry-ingest-service/ # Приём данных с устройств, порт 8003
├── frontend/
│   ├── common/                     # Общие React-компоненты
│   └── apps/
│       ├── experiment-portal/      # SPA, порт 3000
│       ├── auth-proxy/             # Fastify BFF, порт 8080
│       └── sensor-simulator/      # Генерация тестовых данных, порт 8082
├── rc_vehicle/
│   └── firmware/
│       ├── common/                 # Платформенно-независимые алгоритмы
│       ├── esp32_common/           # ESP32-специфичный код
│       ├── esp32_s3/               # Главная прошивка (ESP-IDF)
│       └── tests/                  # GTest тесты
└── telemetry_cli/                  # Python CLI-агент сбора телеметрии (hatchling, Python ≥3.14)
```

**Базы данных:** PostgreSQL 16 + TimescaleDB (телеметрия), порт 5433.
**Очередь:** RabbitMQ. **Кэш:** Redis.
**API:** REST с OpenAPI 3.1 спецификацией в `experiment-service/openapi/`.
**Observability:** Loki (3100) + Alloy (12345) + Grafana (3001, admin/admin).

---

## Команды

### Docker (основной workflow)

```bash
make dev          # docker-compose up (foreground)
make dev-up       # запустить в фоне
make dev-down     # остановить
make dev-logs     # следить за логами
make dev-rebuild  # пересобрать и перезапустить
make dev-status   # показать статус сервисов
make dev-clean    # уничтожить данные (volumes)
```

### Тесты

```bash
make test                    # все тесты (backend + frontend + CLI)
make test-backend            # только pytest
make test-frontend           # только vitest
make test-telemetry-cli      # тесты CLI-агента
make type-check              # mypy для Python + tsc для TS
```

### Запуск отдельного теста

```bash
# Backend (в директории сервиса)
poetry run pytest tests/test_api_experiments.py::test_create_experiment -v

# Frontend
cd projects/frontend/apps/experiment-portal
npm run test -- --run src/api/auth.test.ts
npm run test:watch             # watch-режим
npm run cy:open                # Cypress UI
npm run cy:run                 # headless Cypress E2E

# Auth-proxy
cd projects/frontend/apps/auth-proxy
npm test
```

### Backend (в директории сервиса)

```bash
poetry install --with dev
poetry run pytest
poetry run mypy src/
poetry run ruff check src/
```

### Frontend

```bash
npm ci
npm run dev
npm run test
npm run type-check
npm run build
```

### Telemetry CLI (использует hatchling, не Poetry)

```bash
cd projects/telemetry_cli
python3 -m venv .venv && source .venv/bin/activate
pip install -e .
python -m pytest tests/
```

### SDK-генерация из OpenAPI

```bash
make generate-sdk   # TypeScript Fetch SDK + C++ REST SDK
```

### Миграции БД

```bash
make auth-migrate
make experiment-migrate
make auth-init      # инициализация ролей/схемы auth
```

### Firmware (RC Vehicle)

```bash
make rc-build     # собрать прошивку
make rc-flash     # прошить ESP32-S3
make rc-monitor   # серийный монитор

# GTest (тесты без железа)
cd projects/rc_vehicle/firmware/tests
cmake -B build && cmake --build build
./build/tests
```

### Деплой

```bash
# Продакшен — только по тегу v* на main
git checkout main && git pull origin main
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0

# Ручной деплой
make deploy VM_HOST=<ip> REGISTRY_ID=<id>
```

Подробнее: `docs/branching-model.md`, `docs/deployment-yandex-cloud.md`.

---

## Tech Stack

| Слой | Технология |
|------|-----------|
| Backend | Python 3.12+, aiohttp, asyncpg, Pydantic v2 |
| Frontend | TypeScript, React 19, Vite, MUI |
| Firmware | C++23, ESP-IDF v5.x, ESP32-S3 |
| БД | PostgreSQL 16, TimescaleDB |
| Тесты (backend) | pytest, yandex-taxi-testsuite |
| Тесты (frontend) | Vitest, React Testing Library, Cypress |
| Тесты (firmware) | GTest |
| Линтинг | ruff (Python), mypy, clang-format (C++) |
| CI/CD | GitHub Actions |
| Деплой | Yandex Cloud + Terraform |

---

## Стиль кода

**Python:** PEP 8 + ruff. Строгая типизация через mypy. Структура: `api/ → services/ → repositories/`.

**TypeScript:** Типы генерируются из OpenAPI-спецификации (`experiment-service/openapi/`). Файл типов не редактировать вручную — используй `make generate-sdk`.

**C++ (firmware):** [Google C++ Style Guide](projects/rc_vehicle/docs/cpp_coding_style.md). Форматирование через `.clang-format`. Стандарт C++23.

---

## Важные файлы

| Файл | Назначение |
|------|-----------|
| `docs/experiment-tracking-ts.md` | Главная ТЗ платформы |
| `docs/adr/` | Architecture Decision Records |
| `projects/backend/services/experiment-service/openapi/openapi.yaml` | API-спецификация |
| `projects/rc_vehicle/firmware/common/` | Платформенно-независимые алгоритмы (фильтры, протокол) |
| `projects/rc_vehicle/docs/ts.md` | ТЗ прошивки и MVP-критерии |
| `Makefile` | Все команды сборки/тестирования |
| `docker-compose.yml` | Конфигурация сервисов (порты, переменные) |
| `.env.example` | Шаблон переменных окружения (dev) |
| `.env.docker.example` | Расширенный шаблон для docker-compose |
| `docs/branching-model.md` | Модель веток main/develop и процесс релизов |
| `docs/deployment-yandex-cloud.md` | Деплой в Yandex Cloud, CI/CD, первый релиз |

---

## Firmware — особенности

- Управляющий цикл: **500 Гц** (2 мс). Любые задержки в цикле критичны.
- Failsafe: автоматическое отключение моторов при потере сигнала.
- Ориентация: фильтр Мэджвика + LPF Баттерворта 2-го порядка для гироскопа.
- Конфигурация хранится в **NVS** (Non-Volatile Storage) ESP32.
- Связь: WebSocket (управление) + UART bridge.

---

## Backend — особенности

- Слоевая архитектура: `API routes → Services → Repositories → DB`.
- State machine для переходов состояний Experiment/Run/CaptureSession.
- Идемпотентность POST-запросов через заголовок `Idempotency-Key`.
- RBAC: роли owner/editor/viewer через middleware.
- Миграции: SQL-скрипты в `experiment-service/migrations/`.
- experiment-service включает OpenTelemetry (OTLP exporter).

---

## Среда разработки

- **Node.js:** версия из `.nvmrc` (Node 24 LTS)
- **Python:** 3.12+ для backend-сервисов, 3.14+ для `telemetry_cli`
- **ESP-IDF:** v5.x (для firmware)
- **Docker:** обязателен для запуска всего стека

Переменная `COMPOSE_HTTP_TIMEOUT` задаётся в Makefile (избегает таймаутов при больших образах).

Подробнее: `docs/setup-guide.md`, `docs/local-dev-docker-setup.md`.
