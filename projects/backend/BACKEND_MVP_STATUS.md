# Статус бэкенда для MVP

## Обзор

Документ описывает, что реализовано и чего не хватает в бэкенде (Experiment Service) для MVP функциональности, требуемой фронтендом.

## ✅ Реализовано (критично для MVP)

### 1. CRUD экспериментов (Experiments)
- ✅ `GET /api/v1/experiments` - список экспериментов с фильтрацией и пагинацией
- ✅ `POST /api/v1/experiments` - создание эксперимента
- ✅ `GET /api/v1/experiments/{experiment_id}` - получение эксперимента
- ✅ `PATCH /api/v1/experiments/{experiment_id}` - обновление эксперимента
- ✅ `DELETE /api/v1/experiments/{experiment_id}` - удаление эксперимента
- ✅ `POST /api/v1/experiments/{experiment_id}/archive` - архивирование эксперимента

**Статус:** Полностью реализовано, соответствует требованиям фронтенда.

### 2. CRUD запусков (Runs)
- ✅ `GET /api/v1/experiments/{experiment_id}/runs` - список запусков эксперимента
- ✅ `POST /api/v1/experiments/{experiment_id}/runs` - создание запуска
- ✅ `GET /api/v1/runs/{run_id}` - получение запуска
- ✅ `PATCH /api/v1/runs/{run_id}` - обновление запуска (используется для complete/fail)
- ✅ `POST /api/v1/runs:batch-status` - массовое обновление статусов

**Статус:** Полностью реализовано. Фронтенд использует `PATCH /api/v1/runs/{run_id}` с `status: 'completed'` или `status: 'failed'` для завершения/ошибки запуска.

### 3. Capture Sessions
- ✅ `GET /api/v1/runs/{run_id}/capture-sessions` - список сессий
- ✅ `POST /api/v1/runs/{run_id}/capture-sessions` - создание сессии (старт отсчёта)
- ✅ `POST /api/v1/runs/{run_id}/capture-sessions/{session_id}/stop` - остановка сессии
- ✅ `DELETE /api/v1/runs/{run_id}/capture-sessions/{session_id}` - удаление сессии

**Статус:** Полностью реализовано, соответствует требованиям фронтенда.

### 4. Управление датчиками (Sensors)
- ✅ `GET /api/v1/sensors` - список датчиков с фильтрацией и пагинацией
- ✅ `POST /api/v1/sensors` - регистрация датчика
- ✅ `GET /api/v1/sensors/{sensor_id}` - получение датчика
- ✅ `PATCH /api/v1/sensors/{sensor_id}` - обновление датчика
- ✅ `DELETE /api/v1/sensors/{sensor_id}` - удаление датчика
- ✅ `POST /api/v1/sensors/{sensor_id}/rotate-token` - ротация токена датчика

**Статус:** Полностью реализовано, соответствует требованиям фронтенда.

### 5. Telemetry Ingest (REST)
- ✅ `POST /api/v1/telemetry` - приём телеметрии с токеном датчика

**Статус:** Полностью реализовано. Фронтенд использует этот endpoint для тестовой отправки телеметрии.

### 6. Профили преобразования (Conversion Profiles)
- ✅ `POST /api/v1/sensors/{sensor_id}/conversion-profiles` - создание профиля
- ✅ `GET /api/v1/sensors/{sensor_id}/conversion-profiles` - список профилей
- ✅ `POST /api/v1/sensors/{sensor_id}/conversion-profiles/{profile_id}/publish` - публикация профиля

**Статус:** Реализовано (не используется напрямую фронтендом для MVP, но может быть полезно).

### 7. Метрики (Metrics)
- ✅ `POST /api/v1/runs/{run_id}/metrics` - приём метрик
- ✅ `GET /api/v1/runs/{run_id}/metrics` - запрос метрик

**Статус:** Реализовано (не используется напрямую фронтендом для MVP, но может быть полезно).

---

## ❌ Отсутствует для MVP

### 1. Поиск экспериментов (Search) ✅ РЕАЛИЗОВАНО
**Endpoint:** `GET /api/v1/experiments/search`

**Статус:** ✅ **РЕАЛИЗОВАНО**

**Реализация:**
- ✅ Endpoint `GET /api/v1/experiments/search` реализован
- ✅ Поиск по полям `name` и `description` с использованием ILIKE (case-insensitive)
- ✅ Поддержка параметров: `q` (обязательный), `project_id`, `page`, `page_size`, `limit`, `offset`
- ✅ Пагинация и подсчет общего количества результатов
- ✅ OpenAPI спецификация обновлена

**Где реализовано:**
- `projects/backend/services/experiment-service/src/experiment_service/api/routes/experiments.py` - endpoint
- `projects/backend/services/experiment-service/src/experiment_service/services/experiments.py` - сервис
- `projects/backend/services/experiment-service/src/experiment_service/repositories/experiments.py` - репозиторий
- `projects/backend/services/experiment-service/openapi/paths/experiments.yaml` - OpenAPI спецификация

---

## 🟢 Не критично для MVP (можно отложить)

### 1. Артефакты (Artifacts)
**Endpoints:**
- `POST /api/v1/runs/{run_id}/artifacts` - создание артефакта
- `GET /api/v1/runs/{run_id}/artifacts` - список артефактов
- `POST /api/v1/runs/{run_id}/artifacts/{artifact_id}/approve` - утверждение артефакта

**Статус:** ❌ **НЕ РЕАЛИЗОВАНО** (все endpoints возвращают `501 Not Implemented`)

**Приоритет:** 🟢 **НЕ КРИТИЧНО** для MVP (фронтенд не использует эти endpoints)

**Где реализовано:**
- `projects/backend/services/experiment-service/src/experiment_service/api/routes/artifacts.py` - только заглушки

### 2. WebSocket/SSE стриминг телеметрии
**Endpoint:** `GET /api/v1/telemetry/stream`

**Статус:** ❌ **НЕ РЕАЛИЗОВАНО** (возвращает `1011` с сообщением "Streaming not implemented")

**Приоритет:** 🟢 **НЕ КРИТИЧНО** для MVP (фронтенд не использует WebSocket, только REST ingest)

**Где реализовано:**
- `projects/backend/services/experiment-service/src/experiment_service/api/routes/telemetry.py:50-56` - только заглушка

---

## 📊 Сводная таблица

| Endpoint | Статус | Приоритет | Используется фронтендом |
|----------|--------|-----------|------------------------|
| `GET /api/v1/experiments` | ✅ | Критично | ✅ |
| `POST /api/v1/experiments` | ✅ | Критично | ✅ |
| `GET /api/v1/experiments/{id}` | ✅ | Критично | ✅ |
| `PATCH /api/v1/experiments/{id}` | ✅ | Критично | ✅ |
| `DELETE /api/v1/experiments/{id}` | ✅ | Критично | ✅ |
| `GET /api/v1/experiments/search` | ✅ | Критично | ✅ |
| `GET /api/v1/experiments/{id}/runs` | ✅ | Критично | ✅ |
| `POST /api/v1/experiments/{id}/runs` | ✅ | Критично | ✅ |
| `GET /api/v1/runs/{id}` | ✅ | Критично | ✅ |
| `PATCH /api/v1/runs/{id}` | ✅ | Критично | ✅ |
| `GET /api/v1/runs/{id}/capture-sessions` | ✅ | Критично | ✅ |
| `POST /api/v1/runs/{id}/capture-sessions` | ✅ | Критично | ✅ |
| `POST /api/v1/runs/{id}/capture-sessions/{session_id}/stop` | ✅ | Критично | ✅ |
| `DELETE /api/v1/runs/{id}/capture-sessions/{session_id}` | ✅ | Критично | ✅ |
| `GET /api/v1/sensors` | ✅ | Критично | ✅ |
| `POST /api/v1/sensors` | ✅ | Критично | ✅ |
| `GET /api/v1/sensors/{id}` | ✅ | Критично | ✅ |
| `PATCH /api/v1/sensors/{id}` | ✅ | Критично | ✅ |
| `DELETE /api/v1/sensors/{id}` | ✅ | Критично | ✅ |
| `POST /api/v1/sensors/{id}/rotate-token` | ✅ | Критично | ✅ |
| `POST /api/v1/telemetry` | ✅ | Критично | ✅ |
| `GET /api/v1/telemetry/stream` | ❌ | 🟢 Не критично | ❌ |
| `POST /api/v1/runs/{id}/artifacts` | ❌ | 🟢 Не критично | ❌ |
| `GET /api/v1/runs/{id}/artifacts` | ❌ | 🟢 Не критично | ❌ |
| `POST /api/v1/runs/{id}/artifacts/{id}/approve` | ❌ | 🟢 Не критично | ❌ |

---

## 🎯 Рекомендации для завершения MVP

### Критично (обязательно):
1. ✅ **Реализовать поиск экспериментов** (`GET /api/v1/experiments/search`) - **ВЫПОЛНЕНО**
   - ✅ Endpoint добавлен в `projects/backend/services/experiment-service/src/experiment_service/api/routes/experiments.py`
   - ✅ Метод поиска добавлен в `ExperimentRepository` и `ExperimentService`
   - ✅ Поиск по полям `name` и `description` с использованием ILIKE (case-insensitive)
   - ✅ OpenAPI спецификация обновлена

### Желательно (можно отложить):
2. Артефакты - не критично для MVP, можно оставить заглушки
3. WebSocket стриминг - не критично для MVP, можно оставить заглушку

---

## 📝 Технические детали

### Где искать код:
- **Routes:** `projects/backend/services/experiment-service/src/experiment_service/api/routes/`
- **Services:** `projects/backend/services/experiment-service/src/experiment_service/services/`
- **Repositories:** `projects/backend/services/experiment-service/src/experiment_service/repositories/`
- **OpenAPI:** `projects/backend/services/experiment-service/openapi/`

### Текущая архитектура:
- **Framework:** aiohttp
- **Database:** PostgreSQL 15+ через asyncpg
- **Validation:** Pydantic DTO
- **Idempotency:** через заголовок `Idempotency-Key`
- **RBAC:** через заголовки `X-User-Id`, `X-Project-Id`, `X-Project-Role` (временная реализация)

---

## 📚 Ссылки

- **Frontend MVP Status:** `projects/frontend/MVP_STATUS.md`
- **Auth Service Status:** `projects/backend/AUTH_SERVICE_STATUS.md`
- **Technical Spec:** `docs/experiment-tracking-ts.md`
- **Roadmap:** `docs/experiment-service-roadmap.md`
- **Backend README:** `projects/backend/services/experiment-service/README.md`
- **Auth Service README:** `projects/backend/services/auth-service/README.md`

