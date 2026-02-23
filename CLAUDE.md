# CLAUDE.md — product_dev_course

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
│       └── sensor-simulator/      # Генерация тестовых данных
├── rc_vehicle/
│   └── firmware/
│       ├── common/                 # Платформенно-независимые алгоритмы
│       ├── esp32_common/           # ESP32-специфичный код
│       ├── esp32_s3/               # Главная прошивка (ESP-IDF)
│       └── tests/                  # GTest тесты
└── telemetry_cli/                  # Python CLI-агент сбора телеметрии
```

**Базы данных:** PostgreSQL 16 + TimescaleDB (телеметрия).
**Очередь:** RabbitMQ. **Кэш:** Redis.
**API:** REST с OpenAPI 3.1 спецификацией в `experiment-service/openapi/`.

---

## Команды

### Docker (основной workflow)

```bash
make dev          # docker-compose up (foreground)
make dev-up       # запустить в фоне
make dev-down     # остановить
make dev-logs     # следить за логами
make dev-rebuild  # пересобрать и перезапустить
```

### Тесты

```bash
make test                    # все тесты (backend + frontend + CLI)
make test-backend            # только pytest
make test-frontend           # только vitest
make type-check              # mypy для Python + tsc для TS
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

---

## Tech Stack

| Слой | Технология |
|------|-----------|
| Backend | Python 3.12+, aiohttp, asyncpg, Pydantic v2 |
| Frontend | TypeScript, React 18, Vite, MUI |
| Firmware | C++23, ESP-IDF v5.x, ESP32-S3 |
| БД | PostgreSQL 16, TimescaleDB |
| Тесты (backend) | pytest, yandex-taxi-testsuite |
| Тесты (frontend) | Vitest, React Testing Library |
| Тесты (firmware) | GTest |
| Линтинг | ruff (Python), mypy, clang-format (C++) |
| CI/CD | GitHub Actions |
| Деплой | Yandex Cloud + Terraform |

---

## Стиль кода

**Python:** PEP 8 + ruff. Строгая типизация через mypy. Структура: `api/ → services/ → repositories/`.

**TypeScript:** Типы генерируются из OpenAPI-спецификации (`experiment-service/openapi/`). Файл типов не редактировать вручную.

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
| `.env.example` | Шаблон переменных окружения |

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

---

## Среда разработки

- **Node.js:** версия из `.nvmrc` (LTS)
- **Python:** 3.12+ (проверь `.python-version`)
- **ESP-IDF:** v5.x (для firmware)
- **Docker:** обязателен для запуска всего стека

Подробнее: `docs/setup-guide.md`, `docs/local-dev-docker-setup.md`.