# Frontend: Требования и задачи — RBAC v2 + Скрипты

Ссылки: [rbac-v2-design.md](rbac-v2-design.md), [script-execution-design.md](script-execution-design.md), [tasks-rbac-scripts.md](tasks-rbac-scripts.md)

---

## 1. Контекст: текущее состояние фронта

| Аспект | Сейчас |
|--------|--------|
| Роли | `is_admin` boolean + проектные `owner/editor/viewer` |
| Admin UI | Одна страница `/admin/users` (список пользователей, инвайты) |
| Навигация | `adminOnly: true` — скрывает пункт, если `is_admin = false` |
| Типы | Ручные (`types/index.ts`), `User.is_admin`, `ProjectMember.role` |
| Проверка доступа | `user.is_admin` в компонентах, `role === 'owner'` в модалках |
| Скрипты | Нет UI |
| Аудит | Есть `AuditLog.tsx` для событий run/capture_session, но нет системного аудит-лога |

**Что меняется:**
- `is_admin` исчезает — заменяется на системные роли и permissions
- `ProjectMember.role` string → пользователь может иметь несколько ролей с набором permissions
- Появляются новые сущности: Permission, Role (system + project), AuditEntry, Script, ScriptExecution
- Навигация и доступ к UI определяется permissions, а не boolean-флагом

---

## 2. Требования

### 2.1 Типы и API-клиент

**Новые типы в `types/index.ts`:**

```typescript
// Permissions & Roles
interface Permission {
  id: string                    // 'experiments.create', 'scripts.execute'
  scope_type: 'system' | 'project'
  category: string
  description?: string
}

interface Role {
  id: string
  name: string
  scope_type: 'system' | 'project'
  project_id?: string | null
  is_builtin: boolean
  description?: string
  permissions: string[]          // массив permission IDs
  created_by?: string | null
  created_at: string
  updated_at: string
}

interface UserSystemRole {
  user_id: string
  role_id: string
  role_name: string
  granted_by: string
  granted_at: string
  expires_at?: string | null
}

interface UserProjectRole {
  user_id: string
  project_id: string
  role_id: string
  role_name: string
  granted_by: string
  granted_at: string
  expires_at?: string | null
}

interface EffectivePermissions {
  permissions: string[]
  is_superadmin: boolean
}

// Audit
interface AuditEntry {
  id: string
  timestamp: string
  actor_id: string
  action: string
  scope_type: 'system' | 'project'
  scope_id?: string | null
  target_type?: string | null
  target_id?: string | null
  details: Record<string, any>
  ip_address?: string | null
  user_agent?: string | null
}

interface AuditLogResponse {
  entries: AuditEntry[]
  total: number
  limit: number
  offset: number
}

// Scripts
interface Script {
  id: string
  name: string
  description?: string | null
  target_service: string
  script_type: 'python' | 'shell'
  script_body: string
  parameters: ScriptParameter[]
  timeout_sec: number
  created_by: string
  created_at: string
  updated_at: string
  is_active: boolean
}

interface ScriptParameter {
  name: string
  type: 'string' | 'number' | 'boolean'
  required: boolean
  default?: any
  description?: string
}

interface ScriptExecution {
  id: string
  script_id: string
  script_name?: string
  executed_by: string
  target_service: string
  target_instance?: string | null
  parameters: Record<string, any>
  status: 'pending' | 'running' | 'completed' | 'failed' | 'timeout' | 'cancelled'
  started_at?: string | null
  finished_at?: string | null
  exit_code?: number | null
  stdout?: string | null
  stderr?: string | null
  error_message?: string | null
  created_at: string
}
```

**Обновить `User`:**
```typescript
interface User {
  id: string
  username: string
  email: string
  is_active: boolean
  // is_admin удалён
  password_change_required?: boolean
  created_at: string
  system_roles?: UserSystemRole[]       // опционально, при полном запросе
  effective_permissions?: string[]      // опционально
  is_superadmin?: boolean               // вычисляется из ролей
}
```

**Обновить `ProjectMember`:**
```typescript
interface ProjectMember {
  project_id: string
  user_id: string
  username?: string | null
  roles: UserProjectRole[]              // массив вместо одной role string
  effective_permissions: string[]       // вычисленные permissions
  created_at: string
}
```

**Новые API-функции в `api/auth.ts`:**
```
GET  /api/v1/permissions                              → listPermissions()
GET  /api/v1/users/{id}/effective-permissions          → getEffectivePermissions(userId, projectId?)
GET  /api/v1/system-roles                             → listSystemRoles()
POST /api/v1/system-roles                             → createSystemRole(data)
PATCH /api/v1/system-roles/{id}                       → updateSystemRole(id, data)
DELETE /api/v1/system-roles/{id}                      → deleteSystemRole(id)
POST /api/v1/users/{id}/system-roles                  → grantSystemRole(userId, roleId, expiresAt?)
DELETE /api/v1/users/{id}/system-roles/{roleId}       → revokeSystemRole(userId, roleId)
GET  /api/v1/projects/{pid}/roles                     → listProjectRoles(projectId)
POST /api/v1/projects/{pid}/roles                     → createProjectRole(projectId, data)
PATCH /api/v1/projects/{pid}/roles/{id}               → updateProjectRole(projectId, roleId, data)
DELETE /api/v1/projects/{pid}/roles/{id}              → deleteProjectRole(projectId, roleId)
POST /api/v1/projects/{pid}/members/{uid}/roles       → grantProjectRole(projectId, userId, roleId)
DELETE /api/v1/projects/{pid}/members/{uid}/roles/{rid} → revokeProjectRole(...)
GET  /api/v1/audit-log                                → queryAuditLog(filters)
```

**Новые API-функции в `api/client.ts` (или новый `api/scripts.ts`):**
```
GET    /api/v1/scripts                   → listScripts()
POST   /api/v1/scripts                   → createScript(data)
GET    /api/v1/scripts/{id}              → getScript(id)
PATCH  /api/v1/scripts/{id}              → updateScript(id, data)
DELETE /api/v1/scripts/{id}              → deleteScript(id)
POST   /api/v1/scripts/{id}/execute      → executeScript(id, params)
GET    /api/v1/executions                → listExecutions(filters)
GET    /api/v1/executions/{id}           → getExecution(id)
GET    /api/v1/executions/{id}/logs      → getExecutionLogs(id)
POST   /api/v1/executions/{id}/cancel    → cancelExecution(id)
```

### 2.2 Авторизация на фронте (permissions-based UI)

**Хук `usePermissions()`:**
```typescript
function usePermissions() {
  // Возвращает:
  return {
    systemPermissions: string[]       // system-level permissions текущего пользователя
    projectPermissions: string[]      // permissions в активном проекте
    isSuperadmin: boolean
    hasPermission: (perm: string) => boolean    // проверка с учётом superadmin
    hasAnyPermission: (...perms: string[]) => boolean
    loading: boolean
  }
}
```

- Источник данных: `/auth/me` (расширить ответ) или отдельный запрос `effective-permissions`
- Кэшируется через React Query
- Инвалидируется при смене активного проекта

**Компонент `<PermissionGate>`:**
```tsx
// Рендерит children только если у пользователя есть permission
<PermissionGate permission="scripts.execute">
  <ExecuteButton />
</PermissionGate>

// С fallback
<PermissionGate permission="audit.read" fallback={<NoAccess />}>
  <AuditLog />
</PermissionGate>
```

**Навигация:**
- Заменить `adminOnly: true` → `requiredPermission: 'users.list'` (или массив)
- Пункт меню скрывается если нет нужного permission
- Новые пункты: «Скрипты» (`scripts.execute` или `scripts.manage`), «Аудит» (`audit.read`)

### 2.3 Страница «Администрирование» (рефакторинг `/admin/users`)

Текущая страница `AdminUsers.tsx` → разбивается на вкладки/подстраницы:

**Табы:**
1. **Пользователи** (требует `users.list`)
   - Таблица: username, email, is_active, системные роли (теги), created_at
   - Действия: деактивировать (`users.deactivate`), удалить (`users.delete`), сбросить пароль (`users.reset_password`)
   - Управление ролями: модалка назначения/отзыва системных ролей (`roles.assign`)
   - Убрать переключатель `is_admin` — заменить на назначение ролей

2. **Системные роли** (требует `roles.manage`)
   - Таблица: name, description, is_builtin, количество permissions
   - Создание кастомной роли: имя, описание, выбор permissions из справочника
   - Редактирование кастомной роли (встроенные — только просмотр)
   - Удаление кастомной роли

3. **Инвайты** (требует `users.create`) — без изменений по функционалу

### 2.4 Проектные роли (рефакторинг `ProjectMembersModal`)

- Показывать роли участника как теги (может быть несколько)
- Назначение роли: выбор из встроенных + кастомных ролей проекта
- Создание кастомной проектной роли (owner only): имя + выбор project permissions
- Просмотр effective permissions участника (раскрывающаяся секция)

### 2.5 Страница «Аудит» (новая, `/admin/audit`)

Требует: `audit.read`

- Таблица: timestamp, actor (username), action, scope, target, details
- Фильтры: actor, action (dropdown с группировкой), scope_type, date range
- Пагинация
- Клик на строку → модалка с полными деталями (JSON details, IP, user_agent)
- Фильтры в URL (query params) для шаринга ссылок

### 2.6 Страница «Скрипты» (новая, `/admin/scripts`)

Требует: `scripts.manage` или `scripts.execute`

**Два таба:**

1. **Реестр скриптов** (требует `scripts.manage` для редактирования, `scripts.execute` для просмотра)
   - Таблица: name, target_service, script_type, timeout, is_active, created_at
   - Создание скрипта: форма с полями name, description, target_service (dropdown), script_type, script_body (textarea с monospace шрифтом), parameters (динамическая форма), timeout_sec
   - Редактирование скрипта
   - Деактивация (soft delete)

2. **Выполнения** (требует `scripts.execute` или `scripts.view_logs`)
   - Таблица: script_name, target_service, status (StatusBadge), executed_by, started_at, duration
   - Фильтры: script_id, status, user_id
   - Кнопка «Запустить скрипт» → модалка: выбор скрипта, динамическая форма параметров (генерируется из `script.parameters`), кнопка Execute
   - Детали выполнения: статус, параметры, stdout/stderr (в `<pre>` блоке), exit_code, error_message
   - Кнопка «Отменить» для pending/running
   - Автообновление статуса (polling через React Query refetchInterval)

### 2.7 Компонент PermissionPicker

Переиспользуемый компонент для выбора permissions при создании/редактировании ролей.

- Получает `scope_type` ('system' | 'project') → показывает только релевантные permissions
- Группировка по category (users, roles, scripts, experiments, sensors, ...)
- Чекбоксы: отдельные permissions
- «Выбрать все в категории» / «Снять все»
- Показывает description каждого permission
- Возвращает `string[]` — массив выбранных permission IDs

---

## 3. Задачи

### 3.1 Типы, хук usePermissions, PermissionGate
**~5ч** | зависит от: backend 1.6 | блокирует: 3.2–3.7

- Обновить `types/index.ts`: новые типы (Permission, Role, AuditEntry, Script, ScriptExecution), обновить User (убрать is_admin), обновить ProjectMember
- Создать `api/permissions.ts`: API-функции для permissions, system-roles, project-roles
- Создать `api/scripts.ts`: API-функции для scripts и executions
- Создать `api/audit.ts`: queryAuditLog
- Хук `usePermissions()`: запрос effective-permissions, кэширование, инвалидация при смене проекта
- Компонент `<PermissionGate permission="..." fallback={...}>`
- Обновить `Layout.tsx`: навигация через permissions вместо `adminOnly`
- Обновить `ProtectedRoute.tsx`: сохранять permissions в контексте

### 3.2 Рефакторинг AdminUsers → вкладка «Пользователи»
**~5ч** | зависит от: 3.1

- Рефакторинг `AdminUsers.tsx`: разбить на табы (Пользователи / Системные роли / Инвайты)
- Вкладка «Пользователи»: убрать переключатель is_admin
- Добавить колонку «Роли» — отображение системных ролей как тегов
- Модалка «Управление ролями пользователя»: список текущих ролей, кнопки назначения/отзыва, dropdown с доступными ролями, поле expires_at (опционально)
- Обернуть действия в `<PermissionGate>`: деактивация требует `users.deactivate`, удаление — `users.delete`, роли — `roles.assign`

### 3.3 Вкладка «Системные роли» + PermissionPicker
**~6ч** | зависит от: 3.1

- Компонент `PermissionPicker`: отображение permissions с группировкой по category, чекбоксы, select all/none per category, description
- Вкладка «Системные роли»: таблица ролей (name, builtin badge, permissions count)
- Модалка создания кастомной роли: name, description, PermissionPicker (scope=system)
- Модалка редактирования (только кастомные, встроенные — readonly)
- Модалка просмотра роли: список permissions
- Удаление кастомной роли (с подтверждением)

### 3.4 Рефакторинг ProjectMembersModal
**~4ч** | зависит от: 3.1

- Обновить `ProjectMembersModal`: показывать роли как теги вместо одного dropdown
- Назначение роли: dropdown с встроенными + кастомными ролями проекта
- Отзыв роли: кнопка X на теге роли
- Раскрывающаяся секция «Effective permissions» для каждого участника
- Кнопка «Управление ролями проекта» → модалка с CRUD кастомных проектных ролей (PermissionPicker scope=project)
- Доступ: `project.members.change_role` для назначения, `project.roles.manage` для создания ролей

### 3.5 Страница «Аудит»
**~5ч** | зависит от: 3.1, backend 3.4

- Новая страница `pages/AuditLog.tsx`
- Новый route `/admin/audit`
- Новый пункт навигации (требует `audit.read`)
- Таблица: timestamp, actor (resolve username), action, scope, target
- Панель фильтров: actor (text input), action (MaterialSelect с группировкой), scope_type, date range (два date input)
- Фильтры сохраняются в URL query params
- Пагинация
- Модалка деталей: полный JSON details, IP, user_agent
- React Query: `useQuery(['audit-log', filters])` с keepPreviousData

### 3.6 Страница «Скрипты» — реестр
**~5ч** | зависит от: 3.1, backend 4.2

- Новая страница `pages/Scripts.tsx` с двумя табами
- Новый route `/admin/scripts`
- Новый пункт навигации (требует `scripts.manage` или `scripts.execute`)
- Таб «Реестр»: таблица скриптов (name, target_service, type, timeout, active)
- Модалка создания скрипта: name, description, target_service (MaterialSelect: experiment-service, auth-service, telemetry-ingest-service), script_type (python/shell), script_body (textarea monospace), timeout_sec (number input)
- Динамическая форма параметров: добавить/удалить параметр, поля name, type (string/number/boolean), required (checkbox), default, description
- Модалка редактирования
- Деактивация с подтверждением
- `<PermissionGate permission="scripts.manage">` на кнопках создания/редактирования

### 3.7 Страница «Скрипты» — выполнение
**~6ч** | зависит от: 3.6, backend 4.3

- Таб «Выполнения»: таблица (script_name, target, status badge, user, started_at, duration)
- Фильтры: script (MaterialSelect), status (MaterialSelect), user
- Модалка «Запустить скрипт»:
  - Шаг 1: выбор скрипта из списка активных
  - Шаг 2: динамическая форма параметров (генерируется из `script.parameters` — input type зависит от parameter.type, required/optional, default values)
  - Шаг 3: подтверждение и Execute
  - Результат: execution_id, редирект на детали
- Страница/модалка деталей выполнения: статус (с автообновлением), параметры, stdout в `<pre>` с прокруткой, stderr (красный фон), exit_code, error_message
- Кнопка «Отменить» для pending/running
- Auto-refresh: `refetchInterval: 2000` для pending/running выполнений, останавливается при terminal status

### 3.8 Тесты фронта
**~5ч** | зависит от: 3.2–3.7

- Unit: `usePermissions` — возвращает permissions, hasPermission работает, superadmin bypass
- Unit: `PermissionGate` — рендерит/скрывает children, fallback
- Unit: `PermissionPicker` — группировка, select all, вывод
- Component: `AdminUsers` — рендерится с mock permissions, кнопки скрыты без нужных permissions
- Component: `Scripts` — CRUD, форма параметров
- Component: `AuditLog` — фильтры, пагинация
- E2E (Cypress): логин superadmin → навигация по всем admin-страницам → создание роли → назначение → аудит-лог

---

## 4. Сводка задач

| # | Задача | Оценка | Зависит от |
|---|--------|--------|------------|
| 3.1 | Типы, usePermissions, PermissionGate, API-клиенты | ~5ч | backend 1.6 |
| 3.2 | AdminUsers рефакторинг — вкладка «Пользователи» | ~5ч | 3.1 |
| 3.3 | Вкладка «Системные роли» + PermissionPicker | ~6ч | 3.1 |
| 3.4 | ProjectMembersModal рефакторинг | ~4ч | 3.1 |
| 3.5 | Страница «Аудит» | ~5ч | 3.1, backend 3.4 |
| 3.6 | Страница «Скрипты» — реестр | ~5ч | 3.1, backend 4.2 |
| 3.7 | Страница «Скрипты» — выполнение | ~6ч | 3.6, backend 4.3 |
| 3.8 | Тесты фронта | ~5ч | 3.2–3.7 |

**Итого: ~41ч (~5 рабочих дней)**

## 5. Граф зависимостей

```
backend 1.6 ──► 3.1 ──┬── 3.2 ──┐
                       ├── 3.3 ──┤
                       ├── 3.4 ──┼── 3.8
                       │         │
backend 3.4 ──────────►├── 3.5 ──┤
                       │         │
backend 4.2 ──────────►├── 3.6 ──┤
                       │    │    │
backend 4.3 ───────────┼──►3.7 ──┘
                       │
```

**Параллельность:** После 3.1 задачи 3.2, 3.3, 3.4, 3.5 можно делать параллельно (если backend готов). 3.6 → 3.7 последовательны. 3.8 — в конце.

---

## 6. Нумерация в общем плане

Задачи frontend нумеруются как **фаза 6** в [tasks-rbac-scripts.md](tasks-rbac-scripts.md) (бывшая «Frontend (опционально)»), но теперь с детализацией:

| Общий # | Задача |
|---------|--------|
| 6.1 | Типы, usePermissions, PermissionGate, API (= 3.1) |
| 6.2 | AdminUsers — «Пользователи» (= 3.2) |
| 6.3 | «Системные роли» + PermissionPicker (= 3.3) |
| 6.4 | ProjectMembersModal (= 3.4) |
| 6.5 | Страница «Аудит» (= 3.5) |
| 6.6 | Скрипты — реестр (= 3.6) |
| 6.7 | Скрипты — выполнение (= 3.7) |
| 6.8 | Тесты фронта (= 3.8) |
