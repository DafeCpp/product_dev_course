# RBAC v2 — Система доступов и ролей

## 1. Проблемы текущей системы

| Что есть | Проблема |
|----------|----------|
| `is_admin` boolean | Всё или ничего. Нельзя дать право управлять скриптами, но не удалять пользователей |
| Проектные роли `owner/editor/viewer` | Зашиты в CHECK constraint. Нельзя добавить роль "data_scientist" без миграции |
| Нет аудита | Невозможно узнать кто, когда и зачем выдал роль, удалил пользователя, запустил скрипт |
| Нет делегирования | Owner проекта не может создать кастомную роль с точными правами |
| Права проверяются в коде через `if role != "owner"` | Добавление нового действия требует правок в каждом сервисе |

---

## 2. Целевая модель

Три уровня (scope), единая модель:

```
┌─────────────────────────────────────────────────────┐
│  SYSTEM scope                                        │
│  Глобальные операции: управление пользователями,     │
│  скрипты, конфиги, просмотр аудита                   │
├─────────────────────────────────────────────────────┤
│  PROJECT scope                                       │
│  Операции внутри проекта: эксперименты, сенсоры,     │
│  участники, вебхуки                                  │
├─────────────────────────────────────────────────────┤
│  AUDIT (сквозной слой)                               │
│  Все мутирующие действия записываются                 │
└─────────────────────────────────────────────────────┘
```

### 2.1 Ключевые принципы

1. **Permission = атомарная операция.** `experiments.create`, `scripts.execute`, `users.deactivate`.
2. **Role = именованный набор permissions.** `admin`, `operator`, `project:editor`. Роль не содержит логики — только ссылки на permissions.
3. **Scope** определяет, где действует роль: `system` (глобально) или `project:<id>` (в рамках проекта).
4. **Встроенные роли** покрывают типовые сценарии. Кастомные роли — для точной настройки.
5. **Суперадмин** — единственная роль, которая неявно имеет все permissions (замена `is_admin`).
6. **Deny-by-default.** Если permission не выдан — действие запрещено.
7. **Аудит** — отдельная таблица, append-only, для всех значимых действий.

---

## 3. Модель данных

### 3.1 Permissions (справочник)

Хранятся в коде как enum + seed в БД. Не редактируются пользователями.

```sql
CREATE TABLE permissions (
    id          TEXT PRIMARY KEY,           -- 'experiments.create', 'scripts.execute'
    scope_type  TEXT NOT NULL CHECK (scope_type IN ('system', 'project')),
    category    TEXT NOT NULL,              -- 'experiments', 'scripts', 'users', 'configs'
    description TEXT,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
```

### 3.2 Roles (встроенные + кастомные)

```sql
CREATE TABLE roles (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name        TEXT NOT NULL,
    scope_type  TEXT NOT NULL CHECK (scope_type IN ('system', 'project')),
    project_id  UUID REFERENCES projects(id) ON DELETE CASCADE,  -- NULL для system/шаблонных
    is_builtin  BOOLEAN NOT NULL DEFAULT false,  -- true = нельзя удалить/переименовать
    description TEXT,
    created_by  UUID REFERENCES users(id) ON DELETE SET NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now(),

    -- Уникальность имени: для system — глобально, для project — в рамках проекта
    UNIQUE NULLS NOT DISTINCT (name, scope_type, project_id)
);
```

### 3.3 Role ↔ Permission (many-to-many)

```sql
CREATE TABLE role_permissions (
    role_id       UUID NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    permission_id TEXT NOT NULL REFERENCES permissions(id) ON DELETE CASCADE,
    PRIMARY KEY (role_id, permission_id)
);
```

### 3.4 Назначение ролей пользователям

```sql
-- Системные роли
CREATE TABLE user_system_roles (
    user_id    UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role_id    UUID NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    granted_by UUID NOT NULL REFERENCES users(id),
    granted_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at TIMESTAMPTZ,              -- NULL = бессрочно
    PRIMARY KEY (user_id, role_id)
);

-- Проектные роли (замена project_members.role)
CREATE TABLE user_project_roles (
    user_id    UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    project_id UUID NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    role_id    UUID NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    granted_by UUID NOT NULL REFERENCES users(id),
    granted_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at TIMESTAMPTZ,              -- NULL = бессрочно
    PRIMARY KEY (user_id, project_id, role_id)
);

CREATE INDEX idx_user_project_roles_project ON user_project_roles(project_id);
CREATE INDEX idx_user_project_roles_user ON user_project_roles(user_id);
```

### 3.5 Аудит-лог

```sql
CREATE TABLE audit_log (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    timestamp   TIMESTAMPTZ NOT NULL DEFAULT now(),
    actor_id    UUID NOT NULL,              -- кто совершил действие
    action      TEXT NOT NULL,              -- 'role.grant', 'experiment.create', 'script.execute'
    scope_type  TEXT NOT NULL,              -- 'system' | 'project'
    scope_id    UUID,                       -- project_id или NULL для system
    target_type TEXT,                       -- 'user', 'experiment', 'script', 'role'
    target_id   TEXT,                       -- UUID цели действия
    details     JSONB NOT NULL DEFAULT '{}', -- произвольные данные
    ip_address  INET,
    user_agent  TEXT
);

CREATE INDEX idx_audit_log_actor ON audit_log(actor_id);
CREATE INDEX idx_audit_log_action ON audit_log(action);
CREATE INDEX idx_audit_log_scope ON audit_log(scope_type, scope_id);
CREATE INDEX idx_audit_log_timestamp ON audit_log(timestamp DESC);
CREATE INDEX idx_audit_log_target ON audit_log(target_type, target_id);

-- Партиционирование по месяцам (для TimescaleDB, опционально)
-- SELECT create_hypertable('audit_log', 'timestamp');
```

---

## 4. Встроенные роли и permissions

### 4.1 System permissions

| Permission | Категория | Описание |
|------------|-----------|----------|
| `users.list` | users | Просмотр списка пользователей |
| `users.create` | users | Создание пользователей (инвайты) |
| `users.update` | users | Изменение флагов пользователей |
| `users.deactivate` | users | Деактивация пользователей |
| `users.delete` | users | Удаление пользователей |
| `users.reset_password` | users | Сброс пароля другого пользователя |
| `roles.manage` | roles | Создание/редактирование кастомных ролей |
| `roles.assign` | roles | Назначение системных ролей пользователям |
| `scripts.manage` | scripts | Создание/редактирование/удаление скриптов |
| `scripts.execute` | scripts | Запуск скриптов на сервисах |
| `scripts.view_logs` | scripts | Просмотр логов выполнения |
| `configs.read` | configs | Просмотр динамических конфигов |
| `configs.write` | configs | Изменение динамических конфигов |
| `configs.publish` | configs | Публикация конфигов на сервисы |
| `audit.read` | audit | Просмотр аудит-лога |
| `projects.create` | projects | Создание проектов (если не у всех) |

### 4.2 Project permissions

| Permission | Категория | Описание |
|------------|-----------|----------|
| `project.settings.update` | settings | Изменение настроек проекта |
| `project.settings.delete` | settings | Удаление проекта |
| `project.members.view` | members | Просмотр участников |
| `project.members.invite` | members | Приглашение участников |
| `project.members.remove` | members | Удаление участников |
| `project.members.change_role` | members | Изменение ролей участников |
| `project.roles.manage` | roles | Создание кастомных ролей в проекте |
| `experiments.view` | experiments | Просмотр экспериментов |
| `experiments.create` | experiments | Создание экспериментов |
| `experiments.update` | experiments | Редактирование экспериментов |
| `experiments.delete` | experiments | Удаление экспериментов |
| `experiments.archive` | experiments | Архивация экспериментов |
| `runs.create` | runs | Создание ранов |
| `runs.update` | runs | Обновление ранов |
| `sensors.view` | sensors | Просмотр сенсоров |
| `sensors.manage` | sensors | Создание/настройка сенсоров |
| `sensors.rotate_token` | sensors | Ротация токенов сенсоров |
| `conversion_profiles.manage` | conversion | Управление профилями конвертации |
| `conversion_profiles.publish` | conversion | Публикация профилей |
| `webhooks.manage` | webhooks | Управление вебхуками |
| `capture_sessions.manage` | capture | Управление capture sessions |
| `backfill.create` | backfill | Создание backfill задач |

### 4.3 Встроенные системные роли

| Роль | Permissions | Назначение |
|------|-------------|------------|
| **superadmin** | `*` (все, неявно) | Суперпользователь. Замена `is_admin`. Всегда есть хотя бы один |
| **admin** | `users.*`, `roles.*`, `audit.read`, `projects.create` | Управление пользователями и ролями, но не скрипты/конфиги |
| **operator** | `scripts.*`, `configs.*`, `audit.read` | Оператор: скрипты, конфиги, мониторинг |
| **auditor** | `audit.read`, `users.list` | Только чтение: аудит и список пользователей |

### 4.4 Встроенные проектные роли (шаблоны)

| Роль | Permissions | Эквивалент текущей |
|------|-------------|-------------------|
| **owner** | Все project permissions | `owner` |
| **editor** | Все кроме `*.delete`, `members.remove`, `members.change_role`, `roles.manage`, `conversion_profiles.publish` | `editor` |
| **viewer** | `*.view`, `project.members.view` | `viewer` |

Кастомные проектные роли создаются owner'ом проекта — произвольный набор project permissions.

---

## 5. Как это работает (flows)

### 5.1 Проверка доступа (authorization flow)

```
Запрос → auth-proxy → backend-сервис

1. auth-proxy:
   - Декодирует JWT → user_id
   - Запрашивает у auth-service effective permissions:
     GET /api/v1/users/{user_id}/effective-permissions?project_id={pid}
   - Кэширует результат (Redis, TTL 30s)
   - Инжектирует заголовки:
     X-User-Id: {user_id}
     X-User-Permissions: experiments.create,experiments.view,...  (для project scope)
     X-User-System-Permissions: scripts.execute,audit.read,...    (для system scope)
     X-User-Is-Superadmin: true/false

2. backend-сервис:
   - Читает заголовки
   - Вызывает ensure_permission(user, "experiments.create")
   - Если permission нет и не superadmin → 403
```

### 5.2 Вычисление effective permissions

```python
def get_effective_permissions(user_id, project_id=None):
    """
    Effective permissions = объединение permissions
    всех ролей пользователя в данном scope.
    """
    permissions = set()

    # 1. Системные роли
    system_roles = get_user_system_roles(user_id)  # с учётом expires_at
    for role in system_roles:
        if role.name == "superadmin":
            return ALL_PERMISSIONS  # short-circuit
        permissions |= get_role_permissions(role.id)

    # 2. Проектные роли (если запрошен project scope)
    if project_id:
        project_roles = get_user_project_roles(user_id, project_id)
        for role in project_roles:
            permissions |= get_role_permissions(role.id)

    return permissions
```

### 5.3 Делегирование прав в проекте

```
Owner проекта может:
  1. Назначать встроенные роли (owner/editor/viewer) участникам
  2. Создавать кастомные роли из подмножества project permissions
  3. Назначать кастомные роли участникам

Ограничения:
  - Нельзя создать роль с permissions, которых нет у тебя самого
  - Нельзя назначить роль с бОльшими правами, чем у тебя
  - Owner — единственная роль, дающая project.roles.manage
```

Пример: owner создаёт роль "data_scientist":
```json
{
  "name": "data_scientist",
  "permissions": [
    "experiments.view",
    "experiments.create",
    "experiments.update",
    "runs.create",
    "runs.update",
    "sensors.view",
    "capture_sessions.manage"
  ]
}
```

### 5.4 Аудит

Какие действия аудируются (обязательно):

| Категория | Действия |
|-----------|----------|
| Авторизация | login, logout, password_change, password_reset |
| Пользователи | create, deactivate, delete, update_flags |
| Роли | grant_role, revoke_role, create_custom_role, delete_custom_role |
| Проекты | create, delete, add_member, remove_member, change_member_role |
| Скрипты | execute, cancel |
| Конфиги | update, publish |

Какие действия НЕ аудируются (по умолчанию):
- Чтение (GET-запросы)
- Внутренние технические операции
- Действия по таймерам (cleanup, rotation)

Конфигурируемость: можно включить аудит чтения для compliance (отдельный флаг).

---

## 6. Миграция: clean slate

Система в разработке, пользователей нет — переписываем схему auth-service с нуля.

### 6.1 Стратегия

1. **Удаляем** старую миграцию `001_initial_schema.sql`.
2. **Пишем новую** `001_initial_schema.sql` — единая схема сразу с RBAC v2.
3. **Удаляем** `is_admin` из таблицы `users` (заменён системными ролями).
4. **Удаляем** `project_members` (заменена на `user_project_roles`).
5. Все сервисы сразу работают с permissions — никаких dual headers, никакой обратной совместимости.
6. `make dev-clean && make dev-up` — volumes пересоздаются с чистой БД.

### 6.2 Что меняется в таблице `users`

```sql
CREATE TABLE users (
    id                      UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    username                TEXT NOT NULL UNIQUE,
    email                   TEXT NOT NULL UNIQUE,
    hashed_password         TEXT NOT NULL,
    password_change_required BOOLEAN NOT NULL DEFAULT false,
    -- is_admin убран, заменён на системную роль superadmin
    is_active               BOOLEAN NOT NULL DEFAULT true,
    created_at              TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT now()
);
```

### 6.3 Bootstrap

`bootstrap_admin()` теперь:
1. Создаёт пользователя
2. Назначает ему встроенную системную роль `superadmin`
3. Всё остальное (is_admin checks) заменено на `ensure_permission()`

---

## 7. JWT — изменения

Текущий JWT:
```json
{ "sub": "user_id", "type": "access", "iat": ..., "exp": ... }
```

Новый JWT:
```json
{
  "sub": "user_id",
  "type": "access",
  "iat": ...,
  "exp": ...,
  "sa": true,                              // superadmin flag (опционально)
  "sys": ["scripts.execute", "audit.read"] // system permissions (compact)
}
```

**Важно:** Проектные permissions НЕ включаются в JWT — они зависят от project_id и вычисляются на лету (как сейчас). Системные permissions включаются, т.к. они стабильны и не зависят от контекста запроса.

Если системных permissions мало (< 10) — массив строк. Если много — битовая маска или ссылка на кэш.

---

## 8. API auth-service (новые эндпоинты)

### Permissions

```
GET  /api/v1/permissions                          — справочник всех permissions
GET  /api/v1/users/{id}/effective-permissions      — effective permissions (query: ?project_id=)
```

### System Roles

```
GET    /api/v1/system-roles                        — список системных ролей
POST   /api/v1/system-roles                        — создать кастомную системную роль (superadmin)
PATCH  /api/v1/system-roles/{id}                   — обновить роль
DELETE /api/v1/system-roles/{id}                   — удалить роль (только кастомные)
POST   /api/v1/users/{id}/system-roles             — назначить системную роль
DELETE /api/v1/users/{id}/system-roles/{role_id}   — отозвать системную роль
```

### Project Roles

```
GET    /api/v1/projects/{pid}/roles                — роли в проекте (встроенные + кастомные)
POST   /api/v1/projects/{pid}/roles                — создать кастомную роль (owner)
PATCH  /api/v1/projects/{pid}/roles/{id}           — обновить кастомную роль
DELETE /api/v1/projects/{pid}/roles/{id}           — удалить кастомную роль
POST   /api/v1/projects/{pid}/members/{uid}/roles  — назначить проектную роль
DELETE /api/v1/projects/{pid}/members/{uid}/roles/{role_id} — отозвать
GET    /api/v1/projects/{pid}/members/{uid}/permissions — effective permissions в проекте
```

### Audit

```
GET  /api/v1/audit-log                             — поиск по аудит-логу
     ?actor_id=&action=&scope_type=&scope_id=&from=&to=&limit=&offset=
```

---

## 9. Кэширование

| Что | Где | TTL | Инвалидация |
|-----|-----|-----|-------------|
| Effective permissions пользователя | Redis | 30s | При grant/revoke роли — удалить ключ |
| Справочник permissions | In-memory в auth-service | На старте | Перезапуск (меняется только с деплоем) |
| Роли + role_permissions | Redis | 5 min | При изменении роли |

Ключи Redis:
```
perms:system:{user_id}             → ["scripts.execute", "audit.read"]
perms:project:{user_id}:{project_id} → ["experiments.create", ...]
```

При grant/revoke публикуем event через RabbitMQ → все сервисы инвалидируют локальный кэш.

---

## 10. Влияние на другие сервисы

### experiment-service

Замена `ensure_project_access(require_role=('owner', 'editor'))` на:
```python
ensure_permission(user, "experiments.create")
```

`dependencies.py`:
```python
@dataclass
class UserContext:
    user_id: UUID
    is_superadmin: bool
    system_permissions: set[str]
    project_permissions: dict[UUID, set[str]]  # project_id → permissions

def ensure_permission(user: UserContext, permission: str, project_id: UUID | None = None):
    if user.is_superadmin:
        return
    if project_id:
        perms = user.project_permissions.get(project_id, set())
    else:
        perms = user.system_permissions
    if permission not in perms:
        raise web.HTTPForbidden(reason=f"Missing permission: {permission}")
```

### auth-proxy

Полная замена role injection на permissions injection:
- Вместо `GET /projects/{pid}/members` → `GET /users/{uid}/effective-permissions?project_id={pid}`
- Удалить заголовок `X-Project-Role` — заменён на `X-User-Permissions`
- Результат кэшируется в Redis

### script-service (новый)

Проверяет `scripts.execute`, `scripts.manage`, `scripts.view_logs` через заголовки.

### telemetry-ingest-service

Без изменений (работает по sensor tokens, не по user roles).

---

## 11. Диаграмма ER (итоговая)

```
users
  │
  ├──< user_system_roles >──┐
  │                          │
  ├──< user_project_roles >──┤
  │       │                  │
  │       └── projects       roles
  │                          │
  │                     role_permissions
  │                          │
  │                     permissions
  │
  └──< audit_log
```

---

## 12. Примеры сценариев

### Сценарий 1: DevOps хочет запускать скрипты, но не управлять пользователями

```
Superadmin назначает DevOps-у системную роль "operator":
  POST /api/v1/users/{devops_id}/system-roles
  { "role_id": "<operator-role-uuid>" }

DevOps получает: scripts.*, configs.*, audit.read
Не получает: users.*, roles.*
```

### Сценарий 2: Owner проекта создаёт роль "Test Engineer"

```
POST /api/v1/projects/{pid}/roles
{
  "name": "test_engineer",
  "permissions": [
    "experiments.view", "experiments.create", "experiments.update",
    "runs.create", "runs.update",
    "sensors.view", "capture_sessions.manage"
  ]
}

POST /api/v1/projects/{pid}/members/{uid}/roles
{ "role_id": "<test_engineer-role-uuid>" }
```

### Сценарий 3: Временный доступ аудитора

```
POST /api/v1/users/{auditor_id}/system-roles
{
  "role_id": "<auditor-role-uuid>",
  "expires_at": "2026-04-01T00:00:00Z"
}
```

После 1 апреля роль автоматически перестаёт действовать.

### Сценарий 4: Аудит — кто удалял эксперименты за последнюю неделю?

```
GET /api/v1/audit-log?action=experiment.delete&from=2026-03-01&scope_type=project
```

---

## 13. План реализации

Данных нет — делаем всё сразу, без обратной совместимости.

### Фаза 1: Новая схема auth-service + ядро RBAC
1. Переписать `001_initial_schema.sql` — users (без is_admin), projects, permissions, roles, role_permissions, user_system_roles, user_project_roles, audit_log, revoked_tokens, invite_tokens, password_reset_tokens
2. Seed: встроенные permissions + встроенные роли (superadmin, admin, operator, auditor, project:owner/editor/viewer)
3. Domain models: Permission, Role, AuditEntry (+ обновить User — убрать is_admin)
4. Repositories: PermissionRepo, RoleRepo, UserRoleRepo, AuditRepo
5. Services: PermissionService, AuditService
6. Обновить AuthService: bootstrap_admin → назначение роли superadmin, убрать все `if not requester.is_admin`
7. Обновить ProjectService: убрать project_members, работать через user_project_roles
8. API endpoints (permissions, system-roles, project-roles, audit-log)
9. Тесты

### Фаза 2: auth-proxy + experiment-service
1. auth-proxy: заменить role injection на permissions injection (GET effective-permissions)
2. experiment-service: заменить `ensure_project_access(require_role=...)` на `ensure_permission()`
3. Обновить UserContext — permissions вместо roles
4. Redis-кэш permissions
5. Тесты

### Фаза 3: Аудит
1. Middleware аудита в auth-service (декоратор на service-методы)
2. Middleware аудита в experiment-service
3. API просмотра аудит-лога
4. Тесты

### Фаза 4: Script Service
1. Script Service (сразу на новом RBAC)
2. Script Runner (common module)
3. Интеграция в существующие сервисы
4. Тесты

### Фаза 5 (опционально): Frontend
1. UI управления ролями и permissions
2. UI аудит-лога
3. UI скриптов

---

## 14. Открытые вопросы

1. **Множественные роли в проекте.** Может ли пользователь иметь несколько проектных ролей одновременно (editor + кастомная "test_engineer")? Предложение: да, effective permissions = union. Это проще и гибче.

2. **Wildcard permissions.** Нужен ли `experiments.*` как shorthand? Предложение: нет, только явные permissions — проще аудировать и понимать.

3. **Permission dependencies.** `experiments.delete` подразумевает `experiments.view`? Предложение: нет, каждый permission независим. Встроенные роли просто включают все нужные зависимости.

4. **Rate limiting по permissions.** Нужно ли ограничивать частоту для `scripts.execute`? Предложение: да, через отдельную таблицу лимитов.

5. **Retention аудит-лога.** Сколько хранить? Предложение: 1 год, потом архивация в S3/object storage.

6. **Нужен ли `deny`?** Явный запрет отдельного permission (override над ролью). Предложение: пока нет — усложняет модель. Если нужен — можно добавить позже как `user_permission_overrides` с `grant/deny`.
