# Script Execution Platform — Дизайн-документ

## Обзор

Система удалённого выполнения зарегистрированных скриптов на инстансах backend-сервисов с централизованным управлением, аудитом и гранулярным контролем доступа.

---

## 1. Изменения в системе доступов (auth-service)

### 1.1 Новая таблица: `user_capabilities`

```sql
-- Миграция: 003_user_capabilities.sql
CREATE TABLE user_capabilities (
    user_id    UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    capability TEXT NOT NULL CHECK (capability ~ '^[a-z_]+\.[a-z_]+$'),
    granted_by UUID NOT NULL REFERENCES users(id),
    granted_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (user_id, capability)
);

CREATE INDEX idx_user_capabilities_user ON user_capabilities(user_id);
```

### 1.2 Новые API-эндпоинты auth-service

```
POST   /api/v1/users/{user_id}/capabilities     — выдать capability (admin only)
DELETE /api/v1/users/{user_id}/capabilities/{cap} — отозвать capability (admin only)
GET    /api/v1/users/{user_id}/capabilities      — список capabilities пользователя
```

### 1.3 Изменение JWT access token

Текущая структура:
```json
{ "sub": "user_id", "type": "access", "iat": ..., "exp": ... }
```

Новая структура:
```json
{
  "sub": "user_id",
  "type": "access",
  "iat": ...,
  "exp": ...,
  "adm": true,
  "caps": ["scripts.execute", "scripts.manage", "scripts.view_logs"]
}
```

- `adm` — boolean, true если `is_admin`. Позволяет сервисам проверять админский статус без обращения к auth-service.
- `caps` — массив строк с capabilities пользователя. Пустой массив если нет capabilities.
- При `adm: true` — сервисы считают все capabilities доступными (суперпользователь).

### 1.4 Обратная совместимость

- Существующие сервисы не проверяют `caps`/`adm` — для них ничего не меняется.
- auth-proxy инжектирует `X-User-Capabilities: scripts.execute,scripts.manage` в заголовки (аналогично `X-Project-Role`).

---

## 2. Script Service (новый сервис, порт 8004)

### 2.1 Ответственность

| Функция | Описание |
|---------|----------|
| Script Registry | CRUD зарегистрированных скриптов |
| Execution Dispatcher | Отправка команд на выполнение через RabbitMQ |
| Status Tracker | Отслеживание статуса выполнения (pending → running → completed/failed/timeout) |
| Log Storage | Хранение stdout/stderr и метаданных выполнения |
| Audit Log | Запись кто, когда и что запускал |

### 2.2 Модель данных

```sql
-- Реестр скриптов
CREATE TABLE scripts (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name        TEXT NOT NULL UNIQUE,
    description TEXT,
    target_service TEXT NOT NULL,          -- 'experiment-service', 'auth-service', etc.
    script_type TEXT NOT NULL DEFAULT 'python',  -- 'python', 'shell'
    script_body TEXT NOT NULL,             -- содержимое скрипта
    parameters  JSONB NOT NULL DEFAULT '[]',  -- описание параметров [{name, type, required, default}]
    timeout_sec INT NOT NULL DEFAULT 30,
    created_by  UUID NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    is_active   BOOLEAN NOT NULL DEFAULT true
);

-- Журнал выполнения
CREATE TABLE script_executions (
    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    script_id     UUID NOT NULL REFERENCES scripts(id),
    executed_by   UUID NOT NULL,           -- user_id
    target_service TEXT NOT NULL,
    target_instance TEXT,                  -- идентификатор конкретного инстанса (опционально)
    parameters    JSONB NOT NULL DEFAULT '{}',
    status        TEXT NOT NULL DEFAULT 'pending'
                  CHECK (status IN ('pending', 'running', 'completed', 'failed', 'timeout', 'cancelled')),
    started_at    TIMESTAMPTZ,
    finished_at   TIMESTAMPTZ,
    exit_code     INT,
    stdout        TEXT,
    stderr        TEXT,
    error_message TEXT,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_script_executions_status ON script_executions(status);
CREATE INDEX idx_script_executions_user ON script_executions(executed_by);
CREATE INDEX idx_script_executions_script ON script_executions(script_id);
```

### 2.3 API-эндпоинты

#### Управление скриптами (требует `scripts.manage`)

```
POST   /api/v1/scripts                — создать скрипт
GET    /api/v1/scripts                — список скриптов
GET    /api/v1/scripts/{id}           — получить скрипт
PATCH  /api/v1/scripts/{id}           — обновить скрипт
DELETE /api/v1/scripts/{id}           — деактивировать скрипт (soft delete)
```

#### Запуск скриптов (требует `scripts.execute`)

```
POST   /api/v1/scripts/{id}/execute   — запустить скрипт
POST   /api/v1/executions/{id}/cancel — отменить выполнение
```

Тело запроса `/execute`:
```json
{
  "parameters": { "key": "value" },
  "target_instance": "instance-id (опционально)"
}
```

#### Просмотр результатов (требует `scripts.view_logs` или `scripts.execute`)

```
GET    /api/v1/executions             — список выполнений (фильтры: script_id, status, user_id)
GET    /api/v1/executions/{id}        — статус и результат выполнения
GET    /api/v1/executions/{id}/logs   — stdout/stderr
```

### 2.4 Коммуникация с сервисами

Через **RabbitMQ** (уже есть в стеке):

```
Exchange: script_execution (topic)

Routing keys:
  script.execute.{service_name}   — команда на выполнение
  script.status.{service_name}    — отчёт о статусе от сервиса
  script.cancel.{service_name}    — команда на отмену
```

**Формат сообщения (execute):**
```json
{
  "execution_id": "uuid",
  "script_id": "uuid",
  "script_body": "...",
  "script_type": "python",
  "parameters": {},
  "timeout_sec": 30,
  "executed_by": "user_id"
}
```

**Формат сообщения (status):**
```json
{
  "execution_id": "uuid",
  "status": "running|completed|failed|timeout",
  "exit_code": 0,
  "stdout": "...",
  "stderr": "...",
  "error_message": null
}
```

---

## 3. Script Runner (модуль в каждом сервисе)

### 3.1 Структура

```
projects/backend/common/script_runner/
├── __init__.py
├── runner.py         # ScriptRunner — основной класс
├── executor.py       # Subprocess executor с таймаутами
├── consumer.py       # RabbitMQ consumer
└── models.py         # Pydantic-модели сообщений
```

Размещается в `common/` — общая библиотека для всех backend-сервисов.

### 3.2 Интеграция в сервис

```python
# В startup каждого сервиса:
from common.script_runner import ScriptRunner

runner = ScriptRunner(
    service_name="experiment-service",
    rabbitmq_url=settings.rabbitmq_url,
    work_dir="/app/scripts",        # директория для временных файлов
    max_concurrent=2,               # макс. параллельных выполнений
)

# При старте приложения
await runner.start()

# При остановке
await runner.stop()
```

### 3.3 Механизм выполнения

1. **Consumer** слушает очередь `script.execute.{service_name}`.
2. При получении сообщения — валидирует, отправляет статус `running`.
3. **Executor** запускает скрипт через `asyncio.create_subprocess_exec`:
   - Python-скрипты: `python3 -c <script_body>` (с переданными параметрами через env vars).
   - Shell-скрипты: `bash -c <script_body>`.
4. Применяет таймаут (`timeout_sec`). При превышении — убивает процесс, статус `timeout`.
5. По завершении — отправляет статус `completed`/`failed` с stdout/stderr в `script.status.{service_name}`.

### 3.4 Безопасность

| Мера | Описание |
|------|----------|
| Только зарегистрированные скрипты | Runner не принимает произвольный код — script_id должен быть в реестре |
| Subprocess isolation | Скрипт запускается в отдельном процессе, не в контексте aiohttp |
| Таймауты | Жёсткий таймаут на уровне subprocess (SIGKILL) |
| Ограничение параллелизма | `max_concurrent` — семафор на количество одновременных выполнений |
| Без сетевого доступа (опционально) | Можно запускать с `--network=none` в Docker |
| Аудит | Каждое выполнение логируется с user_id, параметрами, результатом |
| Нет доступа к секретам сервиса | Скрипт запускается с минимальным набором env vars (только параметры) |

---

## 4. Последовательность вызовов (flow)

```
Пользователь                auth-proxy        script-service       RabbitMQ        experiment-service
    │                          │                    │                  │                   │
    │── POST /scripts/X/execute ──►                 │                  │                   │
    │                          │── проверка JWT ──►  │                  │                   │
    │                          │   + caps header     │                  │                   │
    │                          │                    │── проверка caps ──►                   │
    │                          │                    │── создание execution (pending) ──►    │
    │                          │                    │── publish ──────► │                   │
    │                          │                    │   script.execute  │                   │
    │                          │  ◄── 202 Accepted ─┤  .experiment-svc │                   │
    │  ◄── 202 {execution_id} ─┤                    │                  │── deliver ──────► │
    │                          │                    │                  │                   │
    │                          │                    │                  │  ◄── status: running
    │                          │                    │  ◄── consume ────┤                   │
    │                          │                    │── update execution (running)          │
    │                          │                    │                  │                   │
    │                          │                    │                  │   ... выполнение ...
    │                          │                    │                  │                   │
    │                          │                    │                  │  ◄── status: completed
    │                          │                    │  ◄── consume ────┤     + stdout/stderr
    │                          │                    │── update execution (completed)        │
    │                          │                    │                  │                   │
    │── GET /executions/{id} ──►                    │                  │                   │
    │  ◄── {status, logs} ─────┤                    │                  │                   │
```

---

## 5. Docker Compose (добавление)

```yaml
  script-service:
    build:
      context: ./projects/backend
      dockerfile: services/script-service/Dockerfile
    container_name: script-service
    environment:
      APP_PORT: 8004
      DB_HOST: postgres
      DB_PORT: 5432
      DB_NAME: ${POSTGRES_DB:-experiment_db}
      DB_USER: ${SCRIPT_DB_USER:-script_user}
      DB_PASSWORD: ${SCRIPT_DB_PASSWORD:-script_password}
      DB_SCHEMA: script
      RABBITMQ_URL: amqp://${RABBITMQ_USER:-guest}:${RABBITMQ_PASSWORD:-guest}@rabbitmq:5672/
      AUTH_PUBLIC_KEY: ${AUTH_JWT_PUBLIC_KEY}
    ports:
      - "8004:8004"
    depends_on:
      postgres:
        condition: service_healthy
      rabbitmq:
        condition: service_healthy
    networks:
      - experiment-network
    restart: unless-stopped
```

---

## 6. Структура каталогов (итого)

```
projects/backend/
├── common/
│   ├── script_runner/              # NEW — общий модуль для всех сервисов
│   │   ├── __init__.py
│   │   ├── runner.py
│   │   ├── executor.py
│   │   ├── consumer.py
│   │   └── models.py
│   └── ... (существующие утилиты)
└── services/
    ├── auth-service/
    │   └── migrations/
    │       └── 003_user_capabilities.sql   # NEW
    ├── script-service/                     # NEW — весь сервис
    │   ├── Dockerfile
    │   ├── pyproject.toml
    │   ├── migrations/
    │   │   └── 001_initial_schema.sql
    │   └── src/script_service/
    │       ├── __init__.py
    │       ├── app.py
    │       ├── settings.py
    │       ├── api/
    │       │   └── routes/
    │       │       ├── scripts.py
    │       │       └── executions.py
    │       ├── domain/
    │       │   └── models.py
    │       ├── services/
    │       │   ├── script_manager.py
    │       │   ├── execution_dispatcher.py
    │       │   └── dependencies.py
    │       └── repositories/
    │           ├── script_repo.py
    │           └── execution_repo.py
    ├── experiment-service/
    │   └── ... (+ интеграция ScriptRunner в startup)
    └── telemetry-ingest-service/
        └── ... (+ интеграция ScriptRunner в startup)
```

---

## 7. План реализации (этапы)

### Этап 1: Capabilities в auth-service
1. Миграция `003_user_capabilities.sql`.
2. CRUD capabilities в `auth.py` / `projects.py`.
3. Включение `caps` и `adm` в JWT.
4. API-эндпоинты управления capabilities.
5. Обновление auth-proxy: инжекция `X-User-Capabilities`.
6. Тесты.

### Этап 2: Script Service (core)
1. Структура сервиса (по аналогии с experiment-service).
2. Миграция БД (scripts, script_executions).
3. CRUD скриптов.
4. Интеграция с RabbitMQ (publish execute/cancel).
5. Consumer для status-сообщений.
6. API запуска и просмотра результатов.
7. Dockerfile, docker-compose.
8. Тесты.

### Этап 3: Script Runner (common module)
1. Модуль `common/script_runner/`.
2. RabbitMQ consumer (`script.execute.{service}`).
3. Subprocess executor с таймаутами.
4. Интеграция в experiment-service.
5. Интеграция в telemetry-ingest-service.
6. Интеграция в auth-service.
7. Тесты (unit + integration).

### Этап 4: Frontend (опционально)
1. Страница управления скриптами (admin panel).
2. UI запуска с параметрами.
3. Просмотр логов выполнения в реальном времени (WebSocket/SSE).

---

## 8. Открытые вопросы

1. **Хранение скриптов:** в БД (текущий дизайн) vs. в git-репозитории с синхронизацией? Git даёт версионирование, но усложняет деплой.
2. **Масштабирование:** при нескольких инстансах одного сервиса — запускать на всех или на одном? Текущий дизайн: RabbitMQ round-robin → один инстанс. Для broadcast нужен fanout exchange.
3. **Стриминг логов:** stdout/stderr целиком после завершения (текущий дизайн) vs. стриминг через WebSocket в реальном времени?
4. **Sandbox:** нужна ли дополнительная изоляция (nsjail, gVisor) помимо subprocess?
5. **Ограничение ресурсов:** cgroups-лимиты на CPU/RAM для скриптов?
