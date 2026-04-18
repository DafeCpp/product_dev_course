# Задачи: RBAC v2 + Script Execution

Ссылки: [rbac-v2-design.md](rbac-v2-design.md), [script-execution-design.md](script-execution-design.md), [ADR-003](adr/003-script-execution-service.md)

Обозначения: `[блокирует: X]` — задача X не может начаться до завершения этой.

---

## Фаза 1: RBAC v2 в auth-service

### 1.1 Новая схема БД auth-service
**~4ч** | блокирует: 1.2, 1.3, 1.4, 1.5

- Переписать `001_initial_schema.sql` с нуля:
  - `users` (без `is_admin`)
  - `projects` (без `project_members`)
  - `permissions` (справочник)
  - `roles`, `role_permissions`
  - `user_system_roles`, `user_project_roles`
  - `audit_log`
  - `revoked_tokens`, `password_reset_tokens`, `invite_tokens`, `schema_migrations`
- Написать seed-скрипт встроенных permissions (~35 штук) и ролей (superadmin, admin, operator, auditor, project:owner/editor/viewer) с их `role_permissions`
- Обновить `docker-entrypoint-initdb.d` если нужно
- Проверить: `make dev-clean && make dev-up` — БД создаётся без ошибок

### 1.2 Domain models и DTO
**~3ч** | зависит от: 1.1 | блокирует: 1.3, 1.4, 1.5

- Обновить `User` — убрать `is_admin`
- Удалить `ProjectMember` (заменён на `UserProjectRole`)
- Новые модели: `Permission`, `Role`, `RolePermission`, `UserSystemRole`, `UserProjectRole`, `AuditEntry`
- Новые DTO: `RoleResponse`, `PermissionResponse`, `EffectivePermissionsResponse`, `GrantRoleRequest`, `CreateRoleRequest`, `AuditLogEntry`, `AuditLogQuery`

### 1.3 Repositories: permissions, roles, user_roles
**~5ч** | зависит от: 1.2 | блокирует: 1.4

- `PermissionRepo`: `list_all()`, `get_by_ids()`, `list_by_scope()`
- `RoleRepo`: `create()`, `get_by_id()`, `list_system()`, `list_by_project()`, `update()`, `delete()`, `get_permissions()`, `set_permissions()`
- `UserRoleRepo`:
  - `grant_system_role()`, `revoke_system_role()`, `list_system_roles(user_id)`
  - `grant_project_role()`, `revoke_project_role()`, `list_project_roles(user_id, project_id)`
  - `get_effective_permissions(user_id, project_id=None)` — ключевой метод
  - `is_superadmin(user_id)`
  - `list_project_members(project_id)` — замена старого `project_repo.list_members()`

### 1.4 PermissionService + обновление AuthService
**~6ч** | зависит от: 1.3 | блокирует: 1.6, 2.1

**PermissionService** (новый):
- `get_effective_permissions(user_id, project_id)` — с проверкой `expires_at`
- `grant_system_role(grantor, target, role_id)` — проверка что grantor имеет `roles.assign`
- `revoke_system_role(grantor, target, role_id)` — защита последнего superadmin
- `grant_project_role(grantor, project_id, target, role_id)` — проверка что grantor имеет `project.members.change_role` в проекте
- `revoke_project_role(...)` — нельзя снять единственного owner'а
- `create_custom_role(creator, name, scope, permissions, project_id)` — проверка что creator имеет `roles.manage` / `project.roles.manage`
- `update_custom_role(...)`, `delete_custom_role(...)`

**AuthService** (рефакторинг):
- `bootstrap_admin()` → создаёт пользователя + `grant_system_role(superadmin)`
- Заменить все `if not requester.is_admin` → `ensure_permission(requester, "users.*")`
- `create_invite` → требует permission `users.create`
- `list_users` → требует `users.list`
- `update_user` → требует `users.update`
- `delete_user` → требует `users.delete`
- `admin_reset_user` → требует `users.reset_password`

### 1.5 ProjectService рефакторинг
**~4ч** | зависит от: 1.3 | блокирует: 1.6, 2.1

- Убрать зависимость от `project_members` — использовать `user_project_roles`
- `create_project()` → автоматически назначать встроенную роль `owner`
- `add_member()` → `grant_project_role()`
- `remove_member()` → `revoke_project_role()`
- `update_member_role()` → revoke старой + grant новой роли
- `list_members()` → через `UserRoleRepo.list_project_members()`
- Проверки доступа через permissions вместо `role == "owner"`

### 1.6 API endpoints: permissions + roles
**~6ч** | зависит от: 1.4, 1.5 | блокирует: 2.1

Новые route-модули в auth-service:

**`routes/permissions.py`:**
- `GET /api/v1/permissions` — справочник
- `GET /api/v1/users/{id}/effective-permissions?project_id=` — effective permissions

**`routes/system_roles.py`:**
- `GET /api/v1/system-roles`
- `POST /api/v1/system-roles` (superadmin)
- `PATCH /api/v1/system-roles/{id}`
- `DELETE /api/v1/system-roles/{id}`
- `POST /api/v1/users/{id}/system-roles` — назначить
- `DELETE /api/v1/users/{id}/system-roles/{role_id}` — отозвать

**`routes/project_roles.py`:**
- `GET /api/v1/projects/{pid}/roles`
- `POST /api/v1/projects/{pid}/roles`
- `PATCH /api/v1/projects/{pid}/roles/{id}`
- `DELETE /api/v1/projects/{pid}/roles/{id}`
- `POST /api/v1/projects/{pid}/members/{uid}/roles`
- `DELETE /api/v1/projects/{pid}/members/{uid}/roles/{role_id}`

Зарегистрировать все routes в `app.py`.

### 1.7 JWT: включить system permissions
**~2ч** | зависит от: 1.3 | блокирует: 2.1

- `create_access_token(user_id)` → добавить `sa` (bool) и `sys` (list[str]) в claims
- Для этого `create_access_token` должен получать permissions (из PermissionService или передаваться)
- Обновить `get_user_id_from_token()` → вернуть также `sa`, `sys`
- Обновить тесты JWT

### 1.8 Тесты auth-service RBAC
**~6ч** | зависит от: 1.6

- Unit-тесты PermissionService: grant/revoke, effective permissions, superadmin short-circuit, expires_at, защита последнего superadmin
- Unit-тесты AuthService: все операции через permissions вместо is_admin
- Unit-тесты ProjectService: project roles вместо project_members
- API-тесты: все новые endpoints (permissions, system-roles, project-roles)
- API-тесты: проверка что старые endpoints (users CRUD, projects CRUD) работают через новые permissions
- Тест bootstrap_admin → superadmin role

---

## Фаза 2: Интеграция auth-proxy + experiment-service

### 2.1 auth-proxy: permissions injection + script-service routing
**~6ч** | зависит от: 1.6 | блокирует: 2.3

- Заменить логику получения роли (`GET /projects/{pid}/members`) на `GET /users/{uid}/effective-permissions?project_id={pid}`
- Убрать заголовок `X-Project-Role`
- Добавить заголовки:
  - `X-User-Permissions: experiments.create,experiments.view,...`
  - `X-User-System-Permissions: scripts.execute,audit.read,...`
  - `X-User-Is-Superadmin: true/false`
- Добавить proxy-правила для script-service:
  - `/api/v1/scripts/*` → `script-service:8004`
  - `/api/v1/executions/*` → `script-service:8004`
- Обновить типы в TypeScript
- Тесты auth-proxy

### 2.2 experiment-service: переход на ensure_permission
**~5ч** | зависит от: 1.2 | блокирует: 2.3

- Переписать `dependencies.py`:
  - Новый `UserContext` с `permissions: set[str]`, `system_permissions: set[str]`, `is_superadmin: bool`
  - `extract_user()` → читать `X-User-Permissions`, `X-User-System-Permissions`, `X-User-Is-Superadmin`
  - `ensure_permission(user, permission)` вместо `ensure_project_access(require_role=...)`
- Обновить все routes:
  - `experiments.py`: `ensure_permission(user, "experiments.create")` и т.д.
  - `runs.py`, `sensors.py`, `conversion_profiles.py`, `webhooks.py`, `capture_sessions.py`, `backfill.py`
- Убрать `X-Project-Role` из `make_headers()` в тестах → заменить на `X-User-Permissions`

### 2.3 Redis-кэш permissions в auth-proxy
**~3ч** | зависит от: 2.1

- Подключить Redis-клиент в auth-proxy (ioredis)
- Кэшировать результат `effective-permissions` с TTL 30s
- Ключи: `perms:{user_id}:{project_id}` и `perms:sys:{user_id}`
- Инвалидация: при 401/403 — очистить кэш пользователя и retry
- Тесты кэширования

### 2.4 Тесты интеграции
**~4ч** | зависит от: 2.1, 2.2

- Обновить `test_api_rbac.py` — permissions вместо ролей
- Тест: superadmin имеет доступ ко всему
- Тест: пользователь с кастомной ролью (experiments.view + runs.create) — может делать разрешённое, не может остальное
- Тест: пользователь без проектных ролей → 403 на всё
- Тест: expired роль → не работает
- E2E: auth-proxy → experiment-service с реальными permissions

---

## Фаза 3: Аудит

### 3.1 AuditService + AuditRepo
**~4ч** | зависит от: 1.1 | блокирует: 3.2, 3.3

- `AuditRepo`: `log(entry)`, `query(filters, pagination)`, `count(filters)`
- `AuditService`: `log_action(actor_id, action, scope_type, scope_id, target_type, target_id, details, ip, user_agent)`
- Хелпер-декоратор `@audited(action="user.create")` для service-методов
- Или context-manager подход: `async with audit.track(actor, action): ...`

### 3.2 Аудит в auth-service
**~4ч** | зависит от: 3.1

- Добавить аудит в AuthService: login, logout, register, password_change, password_reset, bootstrap
- Добавить аудит в PermissionService: grant_role, revoke_role, create_role, delete_role
- Добавить аудит в ProjectService: create, delete, add_member, remove_member
- Передавать IP и user_agent из request

### 3.3 Аудит в experiment-service
**~3ч** | зависит от: 3.1

- Middleware/декоратор для записи аудита при мутирующих операциях
- experiment.create/update/delete/archive, run.create/update, sensor.create, и т.д.
- Отправлять аудит в auth-service через HTTP или напрямую в БД (если общая) или через RabbitMQ

### 3.4 API просмотра аудит-лога
**~3ч** | зависит от: 3.1 | блокирует: 3.5

- `GET /api/v1/audit-log` с фильтрами: actor_id, action, scope_type, scope_id, target_type, target_id, from, to
- Пагинация: limit + offset
- Требует permission: `audit.read`

### 3.5 Тесты аудита
**~3ч** | зависит от: 3.2, 3.3, 3.4

- Unit: AuditService записывает корректные данные
- API: GET /audit-log с фильтрами возвращает правильные записи
- Интеграция: действие в auth-service → появляется запись в аудит-логе
- Доступ: пользователь без `audit.read` → 403

---

## Фаза 4: Script Service

### 4.1 Scaffold script-service
**~4ч** | зависит от: 1.1 | блокирует: 4.2, 4.3

- Создать структуру каталогов: `services/script-service/` (по аналогии с experiment-service)
- `pyproject.toml` с зависимостями (aiohttp, asyncpg, pydantic, aio-pika)
- `settings.py`, `app.py` (aiohttp application factory)
- `Dockerfile`
- Миграция `001_initial_schema.sql`: таблицы `scripts` (с git-полями), `script_executions`
  - **Важно:** использовать `DB_SCHEMA: script` — отдельная PostgreSQL-схема в общей БД (см. [script-execution-design.md](script-execution-design.md), раздел 5)
- Domain models: `Script` (с git_repo_url, git_path, git_ref), `ScriptExecution`, enums статусов
- Добавить в `docker-compose.yml` + `docker-compose.override.yml`
- `make dev-clean && make dev-up` — сервис стартует

### 4.2 Git Integration + Script CRUD API
**~8ч** | зависит от: 4.1 | блокирует: 4.3

> **Примечание:** GitClient здесь — для валидации git_ref при CRUD (resolve tag/branch → commit, проверка существования файла).
> Отдельный GitClient в `common/script_runner/` (фаза 5) — для загрузки скриптов при выполнении на стороне runner'а.
> Общий код (clone/fetch/checkout) стоит вынести в общую утилиту и переиспользовать.

- `GitClient`: clone, fetch, checkout, read_file, resolve_ref (с кэшированием репозитория)
- Валидация git_path (защита от path traversal)
- `ScriptRepo`: create, get_by_id, list (фильтры: target_service, is_active), update, soft_delete
- `ScriptManager` (service): CRUD с валидацией (имя уникально, git_ref валиден, параметры валидны)
- `routes/scripts.py`:
  - `POST /api/v1/scripts` — требует `scripts.manage`
  - `GET /api/v1/scripts` — требует `scripts.manage` или `scripts.execute`
  - `GET /api/v1/scripts/{id}` — аналогично
  - `PATCH /api/v1/scripts/{id}` — требует `scripts.manage`
  - `DELETE /api/v1/scripts/{id}` — soft delete, `scripts.manage`
  - `POST /api/v1/scripts/{id}/approve` — `scripts.manage`
- `dependencies.py` — `extract_user()`, `ensure_permission()` (аналогично experiment-service)
- Тесты CRUD + git integration

### 4.3 Execution API + RabbitMQ dispatcher
**~6ч** | зависит от: 4.2 | блокирует: 4.4

- `ExecutionRepo`: create, get_by_id, list (фильтры), update_status, update_result
- `ExecutionDispatcher` (service):
  - `execute(user_id, script_id, parameters, target_instance)`:
    - Валидация скрипта (exists, is_active, is_approved)
    - Валидация параметров по schema
    - Получение commit hash из git_ref (resolve tag/branch → commit)
    - Создание записи `script_executions` (status=pending, git_ref_at_execution=commit_hash)
    - Publish в RabbitMQ: `script.execute.{target_service}` (с git_repo_url, git_path, git_ref)
    - Вернуть execution_id
  - `cancel(user_id, execution_id)`: publish `script.cancel.{service}`, статус → cancelled
- RabbitMQ status consumer: слушает `script.status.*`, обновляет `script_executions`
- `routes/executions.py`:
  - `POST /api/v1/scripts/{id}/execute` — `scripts.execute`
  - `POST /api/v1/executions/{id}/cancel` — `scripts.execute`
  - `GET /api/v1/executions` — `scripts.view_logs` или `scripts.execute`
  - `GET /api/v1/executions/{id}` — аналогично
  - `GET /api/v1/executions/{id}/logs` — аналогично
- Тесты

### 4.4 Тесты script-service
**~4ч** | зависит от: 4.3

- Unit: ScriptManager CRUD, валидация параметров, git integration
- Unit: ExecutionDispatcher — создание execution, публикация в RabbitMQ (мок)
- Unit: Status consumer — обновление статуса при получении сообщения
- API: полный цикл CRUD скриптов
- API: запуск, получение статуса, отмена
- RBAC: проверка permissions (scripts.manage vs scripts.execute vs scripts.view_logs)

---

## Фаза 5: Script Runner (common module)

### 5.1 Модуль script_runner
**~6ч** | зависит от: 4.3 | блокирует: 5.2

- `common/script_runner/models.py` — Pydantic-модели сообщений (ExecuteCommand с git-полями, StatusReport, CancelCommand)
- `common/script_runner/git_client.py`:
  - `GitClient(repo_url, token, cache_dir)`
  - `clone()` / `fetch()` — кэширование репозитория
  - `checkout(ref)` — checkout commit/tag/branch
  - `read_file(path)` — чтение файла с валидацией path
  - `resolve_ref(ref)` — resolve tag/branch → commit hash
- `common/script_runner/executor.py`:
  - `execute_script(script_path, script_type, parameters, timeout_sec)` → (exit_code, stdout, stderr)
  - `asyncio.create_subprocess_exec` с таймаутом
  - Очистка env vars (только `PARAM_*` + `PATH`)
  - SIGTERM → SIGKILL при таймауте
- `common/script_runner/consumer.py`:
  - Подключение к RabbitMQ
  - Слушает `script.execute.{service_name}`
  - Слушает `script.cancel.{service_name}`
  - При получении — git checkout, запуск executor, отправка статусов
  - Семафор на `max_concurrent`
- `common/script_runner/runner.py`:
  - `ScriptRunner(service_name, rabbitmq_url, git_repo_url, git_token, max_concurrent)`
  - `start()` / `stop()`
- Unit-тесты executor (subprocess с таймаутами), git_client

### 5.2 Интеграция runner в сервисы
**~3ч** | зависит от: 5.1

- experiment-service: добавить `ScriptRunner` в startup/cleanup
- auth-service: аналогично
- telemetry-ingest-service: аналогично
- Добавить `RABBITMQ_URL`, `SCRIPTS_GIT_REPO_URL`, `SCRIPTS_GIT_TOKEN` в env сервисов
- Smoke-тест: запустить скрипт через API → получить результат

### 5.3 Тесты script runner
**~4ч** | зависит от: 5.2

- Unit: executor — успешный скрипт, ошибка, таймаут, cancel
- Unit: git_client — clone, fetch, checkout, read_file, path traversal защита
- Unit: consumer — получение сообщения, отправка статусов
- Integration: script-service → RabbitMQ → experiment-service runner → статус обратно
- Тест: max_concurrent — третий скрипт ждёт пока один из двух завершится

---

## Фаза 6: Frontend

Детальные требования: [frontend-rbac-scripts-requirements.md](frontend-rbac-scripts-requirements.md)

### 6.1 Типы, usePermissions, PermissionGate, API-клиенты
**~5ч** | зависит от: 1.6 | блокирует: 6.2–6.7

- Обновить `types/index.ts`: Permission, Role, AuditEntry, Script, ScriptExecution; обновить User (убрать is_admin), ProjectMember (roles[] вместо role)
- Создать `api/permissions.ts`, `api/scripts.ts`, `api/audit.ts`
- Хук `usePermissions()`: effective permissions, hasPermission(), isSuperadmin
- Компонент `<PermissionGate permission="...">` — conditional rendering
- Обновить навигацию в `Layout.tsx`: permissions вместо `adminOnly`

### 6.2 AdminUsers рефакторинг — вкладка «Пользователи»
**~5ч** | зависит от: 6.1

- Разбить `AdminUsers.tsx` на табы (Пользователи / Системные роли / Инвайты)
- Убрать переключатель is_admin → колонка «Роли» (теги)
- Модалка назначения/отзыва системных ролей
- Действия обёрнуты в `<PermissionGate>`

### 6.3 Вкладка «Системные роли» + PermissionPicker
**~6ч** | зависит от: 6.1

- Компонент `PermissionPicker`: permissions с группировкой по category, чекбоксы, select all
- Таблица системных ролей, CRUD кастомных
- Встроенные роли — только просмотр

### 6.4 ProjectMembersModal рефакторинг
**~4ч** | зависит от: 6.1

- Роли как теги (множественные), назначение/отзыв
- CRUD кастомных проектных ролей (PermissionPicker scope=project)
- Раскрывающаяся секция effective permissions участника

### 6.5 Страница «Аудит»
**~5ч** | зависит от: 6.1, 3.4

- Новая страница `/admin/audit`, route, навигация (требует `audit.read`)
- Таблица с фильтрами (actor, action, scope, date range), пагинация
- Модалка деталей (JSON details, IP, user_agent)
- Фильтры в URL query params

### 6.6 Страница «Скрипты» — реестр
**~6ч** | зависит от: 6.1, 4.2

- Новая страница `/admin/scripts`, route, навигация (требует `scripts.execute` или `scripts.manage`)
- Два таба: «Реестр» / «Выполнения»
- **Таб «Реестр»**:
  - Таблица: name, target_service, script_type, git_path, git_ref, timeout, is_active, is_approved
  - Фильтры: target_service (dropdown), is_active (checkbox), is_approved (checkbox)
  - Кнопка «Создать скрипт» (требует `scripts.manage`)
  - Модалка создания/редактирования:
    - Поля: name, description, target_service, script_type, **git_repo_url, git_path, git_ref, git_ref_type**
    - Parameters schema editor (динамическое добавление/удаление параметров)
    - Валидация git_path (защита от path traversal)
  - Кнопки действий: Редактировать, Деактивировать, Аппрувнуть (требуют `scripts.manage`)
- `<PermissionGate permission="scripts.manage">` на кнопках CRUD

### 6.7 Страница «Скрипты» — выполнение
**~7ч** | зависит от: 6.6, 4.3

- **Таб «Выполнения»**:
  - Таблица: script_name, status (badge), requested_by, target_service, started_at, duration
  - Фильтры: script_id (dropdown), status (multi-select), requested_by (text), date range
  - Кнопка «Запустить скрипт» (требует `scripts.execute`)
- **Модалка запуска скрипта**:
  - Шаг 1: Выбор скрипта (dropdown с активными аппрувнутыми скриптами)
  - Шаг 2: Параметры (динамическая форма, генерируется из `parameters_schema`)
  - Шаг 3: Target instance (опционально)
  - Шаг 4: Подтверждение (обзор: скрипт, параметры, timeout)
  - Результат: execution_id, редирект на детали
- **Детали выполнения** (модалка или отдельная страница):
  - Header: script name, status badge, execution ID
  - Info: requested_by, started_at, duration, exit_code
  - Parameters: JSON viewer
  - Logs: stdout/stderr в `<pre>` с прокруткой (stderr с красным фоном)
  - Footer: кнопка «Отменить» (для pending/running)
- **Автообновление**: `refetchInterval: 2000` для pending/running, остановка при terminal status

### 6.8 Тесты фронта
**~5ч** | зависит от: 6.2–6.7

- Unit: usePermissions, PermissionGate, PermissionPicker
- Component: AdminUsers с mock permissions, Scripts CRUD, AuditLog фильтры
- E2E (Cypress): полный flow superadmin → роли → назначение → аудит

---

## Сводка

> Статусы актуализированы 2026-03-30 по результатам проверки кодовой базы.

| # | Задача | Оценка | Статус | Зависит от |
|---|--------|--------|--------|------------|
| **Фаза 1** | **RBAC v2 в auth-service** | | | |
| 1.1 | Новая схема БД | ~4ч | ✅ | — |
| 1.2 | Domain models и DTO | ~3ч | ✅ | 1.1 |
| 1.3 | Repositories (permissions, roles, user_roles) | ~5ч | ✅ | 1.2 |
| 1.4 | PermissionService + AuthService рефакторинг | ~6ч | ✅ | 1.3 |
| 1.5 | ProjectService рефакторинг | ~4ч | ✅ | 1.3 |
| 1.6 | API endpoints (permissions, roles) | ~6ч | ✅ | 1.4, 1.5 |
| 1.7 | JWT: system permissions в токене | ~2ч | ✅ | 1.3 |
| 1.8 | Тесты auth-service RBAC | ~6ч | ✅ | 1.6 |
| **Фаза 2** | **Интеграция** | | | |
| 2.1 | auth-proxy: permissions injection + script-service routing | ~6ч | ✅ | 1.6 |
| 2.2 | experiment-service: ensure_permission | ~5ч | ✅ | 1.2 |
| 2.3 | Redis-кэш permissions | ~3ч | ✅ | 2.1 |
| 2.4 | Тесты интеграции | ~4ч | ✅ | 2.1, 2.2 |
| **Фаза 3** | **Аудит** | | | |
| 3.1 | AuditService + AuditRepo | ~4ч | ✅ | 1.1 |
| 3.2 | Аудит в auth-service | ~4ч | ✅ | 3.1 |
| 3.3 | Аудит в experiment-service | ~3ч | ✅ | 3.1 |
| 3.4 | API аудит-лога | ~3ч | ✅ | 3.1 |
| 3.5 | Тесты аудита | ~3ч | ✅ | 3.2, 3.3, 3.4 |
| **Фаза 4** | **Script Service** | | | |
| 4.1 | Scaffold + DB + Docker | ~4ч | ✅ | 1.1 |
| 4.2 | Git Integration + Script CRUD API | ~8ч | ⚠️ CRUD ✅, git_client.py ❌ | 4.1 |
| 4.3 | Execution API + RabbitMQ | ~6ч | ✅ | 4.2 |
| 4.4 | Тесты script-service | ~4ч | ✅ | 4.3 |
| **Фаза 5** | **Script Runner** | | | |
| 5.1 | Модуль script_runner | ~6ч | ⚠️ executor/consumer/runner ✅, git_client.py ❌ | 4.3 |
| 5.2 | Интеграция в сервисы | ~3ч | ✅ | 5.1 |
| 5.3 | Тесты script runner | ~4ч | ✅ | 5.2 |
| **Фаза 6** | **Frontend** | | | |
| 6.1 | Типы, usePermissions, PermissionGate, API | ~5ч | ❌ | 1.6 |
| 6.2 | AdminUsers — «Пользователи» | ~5ч | ❌ | 6.1 |
| 6.3 | «Системные роли» + PermissionPicker | ~6ч | ❌ | 6.1 |
| 6.4 | ProjectMembersModal рефакторинг | ~4ч | ❌ | 6.1 |
| 6.5 | Страница «Аудит» | ~5ч | ❌ | 6.1, 3.4 |
| 6.6 | Скрипты — реестр | ~6ч | ❌ | 6.1, 4.2 |
| 6.7 | Скрипты — выполнение | ~7ч | ❌ | 6.6, 4.3 |
| 6.8 | Тесты фронта | ~5ч | ❌ | 6.2–6.7 |

**Backend (фазы 1–5): выполнено ~107ч из ~110ч. Остаток: `git_client.py` (~3ч)**
**Frontend (фаза 6): не начато, ~48ч**
**Остаток: ~51ч (~6–7 рабочих дней)**

---

## Граф зависимостей

```
1.1 ──► 1.2 ──► 1.3 ──┬── 1.4 ──┐
                       ├── 1.5 ──┼── 1.6 ──┬── 1.8
                       └── 1.7   │         ├── 2.1 ──► 2.3
                                 │         │
                     1.2 ──► 2.2 ┼──────────── 2.4
                                 │
1.1 ──► 3.1 ──┬── 3.2 ──┐       │
              ├── 3.3 ──┼── 3.5 │
              └── 3.4 ──┘       │
                                │
1.1 ──► 4.1 ──► 4.2 ──► 4.3 ──┬── 4.4
                               ├── 5.1 ──► 5.2 ──► 5.3

Frontend:
                    1.6 ──► 6.1 ──┬── 6.2 ──┐
                                  ├── 6.3 ──┤
                                  ├── 6.4 ──┼── 6.8
                           3.4 ──►├── 6.5 ──┤
                           4.2 ──►├── 6.6 ──┤
                           4.3 ──►└──►6.7 ──┘
```

**Параллельные потоки** (backend + frontend разработчик):
- Backend: 1.1 → 1.2 → 1.3 → 1.4/1.5 → 1.6 → 2.1 → 2.2 → 2.4 → 4.1–4.4 → 5.1–5.3
- Frontend: (ждёт 1.6) → 6.1 → 6.2/6.3/6.4 параллельно → (ждёт 3.4) → 6.5 → (ждёт 4.2) → 6.6 → 6.7 → 6.8

**Примечание:** Фаза 3 (Аудит) зависит только от 1.1, поэтому 3.1 можно начать сразу после 1.1 параллельно с 1.2+. При наличии второго разработчика аудит не лежит на критическом пути.

При одном разработчике — критический путь: **1.1 → 1.2 → 1.3 → 1.4 → 1.6 → 2.1 → 2.2 → 2.4 → 4.1 → 4.2 → 4.3 → 5.1 → 5.2 → 5.3 → 6.1 → 6.6 → 6.7** (~85ч, ~11 дней). Аудит (фаза 3), тесты и остальной frontend идут в параллель или в конце.
