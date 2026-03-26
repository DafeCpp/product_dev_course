# Script Execution Platform — Дизайн-документ

## Обзор

Система удалённого выполнения зарегистрированных скриптов на инстансах backend-сервисов с централизованным управлением, аудитом и гранулярным контролем доступа.

### Ключевые принципы

1. **Скрипты хранятся в git-репозитории** — не в БД. Это даёт:
   - Версионирование кода скриптов
   - Code review через PR/MR
   - Audit trail изменений
   - Откат к предыдущим версиям
   - CI/CD для скриптов (линтеры, тесты)

2. **Script Registry в БД** — хранит только метаданные:
   - Ссылка на файл в репозитории (path + commit/tag)
   - Версия (git commit hash или tag)
   - Параметры (schema)
   - Метаданные (описание, timeout, target_service)

3. **Изолированное выполнение** — subprocess с таймаутами, без доступа к секретам сервиса.

4. **Централизованный аудит** — кто, когда, какой скрипт запустил, с какими параметрами и результатом.

---

## 1. Система доступов (RBAC v2)

> **Примечание:** Ранее в этом разделе описывалась упрощённая модель `user_capabilities`.
> Она заменена на полноценный RBAC v2 с ролями и гранулярными permissions.
> Полное описание: [rbac-v2-design.md](rbac-v2-design.md), задачи: [tasks-rbac-scripts.md](tasks-rbac-scripts.md).

### 1.1 Модель доступа (краткое описание)

Доступ к скриптам контролируется через permissions RBAC v2:

| Permission | Описание |
|------------|----------|
| `scripts.manage` | CRUD скриптов, аппрув |
| `scripts.execute` | Запуск скриптов |
| `scripts.view_logs` | Просмотр результатов выполнения |

Permissions назначаются через роли (`roles` → `role_permissions`), роли назначаются пользователям (`user_system_roles`).
Superadmin имеет все permissions автоматически.

### 1.2 JWT access token

```json
{
  "sub": "user_id",
  "type": "access",
  "iat": ...,
  "exp": ...,
  "sa": true,
  "sys": ["scripts.execute", "scripts.manage", "scripts.view_logs"]
}
```

- `sa` — boolean, true если superadmin.
- `sys` — массив system permissions пользователя.
- При `sa: true` — сервисы считают все permissions доступными.

### 1.3 Заголовки auth-proxy

auth-proxy инжектирует в запросы к downstream-сервисам:
- `X-User-Permissions: <project-scoped permissions>` (для запросов с project_id)
- `X-User-System-Permissions: scripts.execute,scripts.manage,...`
- `X-User-Is-Superadmin: true/false`

---

## 2. Script Service (новый сервис, порт 8004)

### 2.1 Ответственность

| Функция | Описание |
|---------|----------|
| Script Registry | CRUD метаданных зарегистрированных скриптов (ссылки на git) |
| Git Integration | Синхронизация с git-репозиторием, загрузка скриптов по commit hash |
| Execution Dispatcher | Отправка команд на выполнение через RabbitMQ |
| Status Tracker | Отслеживание статуса выполнения (pending → running → completed/failed/timeout) |
| Log Storage | Хранение stdout/stderr и метаданных выполнения |
| Audit Log | Запись кто, когда и что запускал |

### 2.2 Модель данных

```sql
-- Реестр скриптов (метаданные, код в git)
CREATE TABLE scripts (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name        TEXT NOT NULL UNIQUE,
    description TEXT,
    target_service TEXT NOT NULL,          -- 'experiment-service', 'auth-service', etc.
    script_type TEXT NOT NULL DEFAULT 'python',  -- 'python', 'bash', 'javascript'
    
    -- Git repository reference
    git_repo_url    TEXT NOT NULL,         -- https://github.com/org/scripts-repo.git
    git_path        TEXT NOT NULL,         -- path/to/script.py внутри репозитория
    git_ref         TEXT NOT NULL,         -- commit hash или tag (например, "v1.2.3" или "abc123...")
    git_ref_type    TEXT NOT NULL DEFAULT 'commit',  -- 'commit' | 'tag' | 'branch'
    
    parameters_schema JSONB NOT NULL DEFAULT '[]',  -- описание параметров [{name, type, required, default, description}]
    timeout_sec     INT NOT NULL DEFAULT 30,
    is_active       BOOLEAN NOT NULL DEFAULT true,
    is_approved     BOOLEAN NOT NULL DEFAULT false,  -- флаг code review/approval
    approved_by     UUID,                            -- кто аппрувнул
    approved_at     TIMESTAMPTZ,
    
    created_by      UUID NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_scripts_target_service ON scripts(target_service);
CREATE INDEX idx_scripts_active ON scripts(is_active) WHERE is_active = true;
CREATE INDEX idx_scripts_approved ON scripts(is_approved) WHERE is_approved = true;

-- Журнал выполнения
CREATE TABLE script_executions (
    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    script_id     UUID NOT NULL REFERENCES scripts(id),
    requested_by  UUID NOT NULL,           -- user_id
    target_service TEXT NOT NULL,
    target_instance TEXT,                  -- идентификатор конкретного инстанса (опционально)
    parameters    JSONB NOT NULL DEFAULT '{}',
    git_ref_at_execution TEXT,            -- commit hash на момент запуска (для аудита)
    
    status        TEXT NOT NULL DEFAULT 'pending'
                  CHECK (status IN ('pending', 'running', 'completed', 'failed', 'timeout', 'cancelled')),
    started_at    TIMESTAMPTZ,
    finished_at   TIMESTAMPTZ,
    exit_code     INT,
    stdout        TEXT,
    stderr        TEXT,
    error_message TEXT,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_script_executions_status ON script_executions(status);
CREATE INDEX idx_script_executions_user ON script_executions(requested_by);
CREATE INDEX idx_script_executions_script ON script_executions(script_id);
CREATE INDEX idx_script_executions_created ON script_executions(created_at DESC);
```

### 2.3 Git Integration

**Хранение скриптов:**
- Скрипты хранятся в отдельном git-репозитории (например, `platform-scripts`)
- Структура репозитория:
  ```
  platform-scripts/
  ├── experiments/
  │   ├── cleanup_old_runs.py
  │   ├── export_metrics.py
  │   └── archive_experiment.py
  ├── auth/
  │   ├── reset_user_password.py
  │   └── deactivate_inactive_users.py
  └── telemetry/
      ├── backfill_missing_data.py
      └── recalibrate_sensor.py
  ```

**Синхронизация:**
- Script Service кэширует локальную копию репозитория
- При создании/обновлении скрипта — git fetch для получения актуального commit
- При выполнении — checkout нужного commit для воспроизводимости

**Безопасность:**
- Доступ к репозиторию через HTTPS с токеном или SSH ключ
- Токен/ключ хранится в secrets manager или env vars
- Валидация пути (защита от path traversal: `../`)

### 2.4 API-эндпоинты

#### Управление скриптами (требует `scripts.manage`)

```
POST   /api/v1/scripts                — создать скрипт (метаданные + git reference)
GET    /api/v1/scripts                — список скриптов
GET    /api/v1/scripts/{id}           — получить скрипт
PATCH  /api/v1/scripts/{id}           — обновить скрипт
DELETE /api/v1/scripts/{id}           — деактивировать скрипт (soft delete)
POST   /api/v1/scripts/{id}/approve   — аппрувнуть скрипт (после code review)
```

Тело запроса `POST /api/v1/scripts`:
```json
{
  "name": "cleanup-old-runs",
  "description": "Удаляет старые runs старше 90 дней",
  "target_service": "experiment-service",
  "script_type": "python",
  "git_repo_url": "https://github.com/org/platform-scripts.git",
  "git_path": "experiments/cleanup_old_runs.py",
  "git_ref": "v1.0.0",
  "git_ref_type": "tag",
  "parameters_schema": [
    {"name": "days", "type": "number", "required": true, "default": 90, "description": "Количество дней"}
  ],
  "timeout_sec": 60
}
```

#### Запуск скриптов (требует `scripts.execute`)

```
POST   /api/v1/scripts/{id}/execute   — запустить скрипт
POST   /api/v1/executions/{id}/cancel — отменить выполнение
```

Тело запроса `/execute`:
```json
{
  "parameters": {"days": 120},
  "target_instance": "instance-id (опционально)"
}
```

#### Просмотр результатов (требует `scripts.view_logs` или `scripts.execute`)

```
GET    /api/v1/executions             — список выполнений (фильтры: script_id, status, user_id)
GET    /api/v1/executions/{id}        — статус и результат выполнения
GET    /api/v1/executions/{id}/logs   — stdout/stderr
```

### 2.5 Коммуникация с сервисами

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
  "git_repo_url": "https://github.com/org/platform-scripts.git",
  "git_path": "experiments/cleanup_old_runs.py",
  "git_ref": "abc123...",
  "script_type": "python",
  "parameters": {"days": 120},
  "timeout_sec": 60,
  "requested_by": "user_id"
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
├── git_client.py     # Git client для загрузки скриптов из репозитория
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
    git_repo_url=settings.scripts_git_repo_url,  # URL репозитория скриптов
    git_token=settings.scripts_git_token,         # Токен для доступа к git
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
3. **Git Client** загружает скрипт из репозитория:
   - Clone/fetch репозитория (если ещё не кэширован)
   - Checkout нужного commit/tag
   - Чтение файла по `git_path`
4. **Executor** запускает скрипт через `asyncio.create_subprocess_exec`:
   - Python-скрипты: `python3 /path/to/script.py` (с переданными параметрами через env vars `PARAM_*`)
   - Bash-скрипты: `bash /path/to/script.sh`
   - JavaScript: `node /path/to/script.js`
5. Применяет таймаут (`timeout_sec`). При превышении — убивает процесс, статус `timeout`.
6. По завершении — отправляет статус `completed`/`failed` с stdout/stderr в `script.status.{service_name}`.

### 3.4 Безопасность

| Мера | Описание |
|------|----------|
| Только зарегистрированные скрипты | Runner не выполняет произвольный код — script_id должен быть в реестре, код загружается из доверенного git |
| Git repository whitelist | Runner принимает скрипты только из настроенного репозитория (env var `SCRIPTS_GIT_REPO_URL`) |
| Commit hash verification | При выполнении проверяется что git_ref — валидный commit hash (защита от подмены) |
| Subprocess isolation | Скрипт запускается в отдельном процессе, не в контексте aiohttp |
| Таймауты | Жёсткий таймаут на уровне subprocess (SIGTERM → SIGKILL) |
| Ограничение параллелизма | `max_concurrent` — семафор на количество одновременных выполнений |
| Без доступа к секретам сервиса | Скрипт запускается с минимальным набором env vars (только `PARAM_*` и `PATH`) |
| Аудит | Каждое выполнение логируется с user_id, параметрами, результатом, git_ref |
| Path traversal защита | Валидация `git_path` — запрет `..`, абсолютных путей, symlink |

---

## 4. Последовательность вызовов (flow)

```
Пользователь                auth-proxy        script-service       RabbitMQ        experiment-service
    │                          │                    │                  │                   │
    │── POST /scripts/X/execute ──►                 │                  │                   │
    │                          │── проверка JWT ──►  │                  │                   │
    │                          │   + permissions     │                  │                   │
    │                          │                    │── проверка perms ──►                   │
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
│   │   ├── git_client.py
│   │   └── models.py
│   └── ... (существующие утилиты)
└── services/
    ├── auth-service/
    │   └── migrations/
    │       └── 001_initial_schema.sql      # REWRITTEN (RBAC v2)
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

### Этап 1: RBAC v2 в auth-service
> Детальная разбивка: [tasks-rbac-scripts.md](tasks-rbac-scripts.md), фазы 1–2.

1. Новая схема БД (permissions, roles, user_system_roles, user_project_roles).
2. Domain models, repositories, PermissionService.
3. Включение `sa` и `sys` в JWT.
4. API-эндпоинты permissions, system-roles, project-roles.
5. Обновление auth-proxy: инжекция `X-User-Permissions`, `X-User-System-Permissions`, `X-User-Is-Superadmin`.
6. Переход experiment-service на `ensure_permission()`.
7. Тесты.

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

1. **Git repository strategy:** один общий репозиторий для всех скриптов vs. репозиторий на сервис?
   **Решение:** один `platform-scripts` с папками по сервисам. Упрощает управление и code review.
2. **Code review процесс:** как интегрировать аппрув скриптов в существующий workflow?
   **Решение:** внутренний аппрув через UI (`POST /scripts/{id}/approve`). GitHub webhook — отложено на будущее.
3. **Масштабирование:** при нескольких инстансах одного сервиса — запускать на всех или на одном?
   **Решение:** RabbitMQ round-robin → один инстанс. Достаточно для текущего масштаба.
4. **Стриминг логов:** stdout/stderr целиком после завершения vs. стриминг в реальном времени?
   **Решение:** целиком после завершения (MVP). Frontend polling каждые 2с для pending/running. WebSocket-стриминг — post-MVP.
5. **Sandbox:** нужна ли дополнительная изоляция (nsjail, gVisor) помимо subprocess?
   — Открыт. Для MVP достаточно subprocess isolation + env var очистка. Переоценить при расширении на внешние скрипты.
6. **Ограничение ресурсов:** cgroups-лимиты на CPU/RAM для скриптов?
   — Открыт. Не критично для MVP (таймаут + max_concurrent покрывают основные риски).
7. **Git token rotation:** как автоматически обновлять токены доступа к git?
   — Открыт. Текущий план: env var `SCRIPTS_GIT_TOKEN`, ротация через перезапуск/CI.

---

## 9. Frontend: Админка управления скриптами

### 9.1 Страница «Скрипты» — Реестр (`/admin/scripts`)

**Требует permission:** `scripts.manage` (для CRUD) или `scripts.execute` (только просмотр)

**Функциональность:**

| Компонент | Описание |
|-----------|----------|
| Таблица скриптов | name, target_service, script_type, git_path, git_ref, timeout, is_active, is_approved |
| Фильтры | target_service (dropdown), is_active (checkbox), is_approved (checkbox) |
| Кнопка «Создать скрипт» | Открывает модалку создания (требует `scripts.manage`) |
| Кнопки действий | Редактировать, Деактивировать, Аппрувнуть (требуют `scripts.manage`) |

**Модалка создания/редактирования скрипта:**

```
Поля:
├─ name (text, required, unique)
├─ description (textarea, optional)
├─ target_service (dropdown, required)
│  └─ experiment-service, auth-service, telemetry-ingest-service
├─ script_type (dropdown, required)
│  └─ python, bash, javascript
├─ git_repo_url (text, required)
│  └─ https://github.com/org/platform-scripts.git
├─ git_path (text, required)
│  └─ experiments/cleanup_old_runs.py
├─ git_ref (text, required)
│  └─ v1.0.0 (tag) или abc123... (commit)
├─ git_ref_type (dropdown, required)
│  └─ tag, commit, branch
├─ parameters_schema (dynamic list)
│  ├─ name (text)
│  ├─ type (dropdown: string, number, boolean)
│  ├─ required (checkbox)
│  ├─ default (input, зависит от type)
│  └─ description (text)
└─ timeout_sec (number, default: 30)

Кнопки: [Отмена] [Сохранить]
```

**Валидация:**
- `git_path` не должен содержать `..`, начинаться с `/`, быть symlink
- `git_ref` должен быть валидным format (commit hash или semver для tag)
- `parameters_schema` — валидный JSON array

### 9.2 Страница «Выполнения» (`/admin/scripts/executions`)

**Требует permission:** `scripts.execute` или `scripts.view_logs`

**Функциональность:**

| Компонент | Описание |
|-----------|----------|
| Таблица выполнений | script_name, status (badge), requested_by, target_service, started_at, duration |
| Фильтры | script_id (dropdown), status (multi-select), requested_by (text), date range |
| Кнопка «Запустить скрипт» | Открывает модалку запуска (требует `scripts.execute`) |
| Кнопки действий | Просмотр деталей, Отменить (для pending/running) |

**Модалка запуска скрипта:**

```
Шаг 1: Выбор скрипта
└─ dropdown с активными аппрувнутыми скриптами

Шаг 2: Параметры (генерируется динамически из parameters_schema)
├─ PARAM_NAME (input type зависит от parameter.type)
│  ├─ number → <input type="number" />
│  ├─ string → <input type="text" />
│  └─ boolean → <input type="checkbox" />
└─ Подсказки из parameter.description

Шаг 3: Target instance (опционально)
└─ input text (для запуска на конкретном инстансе)

Шаг 4: Подтверждение
├─ Обзор: скрипт, параметры, timeout
└─ Кнопка [Запустить]

Результат:
└─ execution_id, редирект на детали выполнения
```

**Детали выполнения (модалка или отдельная страница):**

```
Header:
├─ Script: cleanup-old-runs
├─ Status: <StatusBadge status="running" />
└─ Execution ID: abc-123

Info:
├─ Requested by: admin
├─ Started at: 2025-03-20 14:30:00
├─ Duration: 5s
└─ Exit code: —

Parameters:
└─ JSON viewer с параметрами

Logs:
├─ stdout (<pre> с прокруткой, max-height: 300px)
└─ stderr (<pre> с красным фоном, max-height: 300px)

Footer:
└─ Кнопка [Отменить] (для pending/running, disabled для completed/failed)
```

**Автообновление:**
- Для статусов `pending`/`running` — polling каждые 2 секунды
- Остановка polling при terminal статусе (`completed`, `failed`, `timeout`, `cancelled`)

### 9.3 Компоненты

**Новые компоненты:**

```typescript
// components/scripts/ScriptList.tsx
// Таблица скриптов с фильтрами и действиями

// components/scripts/ScriptForm.tsx
// Форма создания/редактирования скрипта с валидацией git-полей

// components/scripts/ExecutionList.tsx
// Таблица выполнений с фильтрами

// components/scripts/ExecuteScriptModal.tsx
// Модалка запуска: выбор скрипта → параметры → подтверждение

// components/scripts/ExecutionDetailsModal.tsx
// Детали выполнения: статус, логи, параметры

// components/scripts/GitPathInput.tsx
// Input для git_path с валидацией (защита от path traversal)

// components/scripts/ParametersSchemaEditor.tsx
// Редактор parameters_schema (добавление/удаление параметров)
```

### 9.4 API интеграция

**Новые функции в `api/scripts.ts`:**

```typescript
export const scriptsApi = {
  // Scripts CRUD
  listScripts: (filters?: { target_service?: string; is_active?: boolean }) =>
    GET('/api/v1/scripts', { params: filters }),

  getScript: (id: string) =>
    GET(`/api/v1/scripts/${id}`),

  createScript: (data: ScriptCreateRequest) =>
    POST('/api/v1/scripts', data),

  updateScript: (id: string, data: ScriptUpdateRequest) =>
    PATCH(`/api/v1/scripts/${id}`, data),

  deleteScript: (id: string) =>
    DELETE(`/api/v1/scripts/${id}`),

  approveScript: (id: string) =>
    POST(`/api/v1/scripts/${id}/approve`),

  // Executions
  listExecutions: (filters?: { script_id?: string; status?: string[]; requested_by?: string }) =>
    GET('/api/v1/executions', { params: filters }),

  getExecution: (id: string) =>
    GET(`/api/v1/executions/${id}`),

  executeScript: (id: string, data: ExecuteScriptRequest) =>
    POST(`/api/v1/scripts/${id}/execute`, data),

  cancelExecution: (id: string) =>
    POST(`/api/v1/executions/${id}/cancel`),

  getExecutionLogs: (id: string) =>
    GET(`/api/v1/executions/${id}/logs`),
}
```

### 9.5 Навигация

**Добавить пункт в меню админки:**

```typescript
// Layout.tsx или навигационный конфиг
{
  path: '/admin/scripts',
  label: 'Скрипты',
  icon: <CodeIcon />,
  requiredPermission: 'scripts.execute',  // или 'scripts.manage'
  children: [
    { path: '', label: 'Реестр' },
    { path: 'executions', label: 'Выполнения' },
  ]
}
```
