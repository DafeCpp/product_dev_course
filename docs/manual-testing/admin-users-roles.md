# Сценарии: Admin — Пользователи и Системные роли

Админ-разделы **Пользователи** (`/admin/users`) и **Системные роли** (`/admin/system-roles`):
управление аккаунтами, инвайты, admin-reset, деактивация, CRUD системных ролей и назначений,
запреты по RBAC. Проектные роли owner/editor/viewer — в [`rbac-permissions.md`](rbac-permissions.md)
(здесь **не дублируются**).

**Где реализовано:**
- Пользователи/инвайты (auth-service через auth-proxy): `GET/PATCH/DELETE /auth/admin/users`,
  `POST /auth/admin/users/:id/reset`, `POST /auth/admin/invites` (`src/api/auth.ts`); модалки
  `UserProfileModal.tsx`, `UserRolesModal.tsx`.
- Системные роли: `GET/POST /api/v1/system-roles`, `PATCH/DELETE /api/v1/system-roles/{id}`;
  назначения `POST /api/v1/users/{userId}/system-roles`, `DELETE .../system-roles/{roleId}`
  (`src/api/permissions.ts`, `SystemRoles.tsx`).
- Доступ к разделу: nav требует `users.list` / `roles.manage` / `roles.assign`.

**Предусловие:** авторизован как `admin` (`superadmin`).

---

### TC-USERS-01 — Список пользователей и вкладки
**Предусловие:** открыт `/admin/users`.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Открыть раздел | Вкладки Активные / Деактивированные / Инвайты; колонки Пользователь/Email/Роль/Дата регистрации/Статус; `200` |
| 2 | Открыть профиль пользователя (`UserProfileModal`) | Данные аккаунта отображаются |

---

### TC-USERS-02 — Инвайт и регистрация по нему
**Предусловие:** вкладка «Инвайты».

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Создать инвайт (email-подсказка опц., срок в часах) | `POST /auth/admin/invites` → токен в таблице; «Копировать» работает (фолбэк BUG-F-002) |
| 2 | Зарегистрироваться по токену | Аккаунт создан; инвайт «Использован» (`used_at`, см. BUG-F-011) |
| 3 | Повторно использовать тот же токен | Отклонено (single-use, см. [`idempotency-auth-hardening.md`](idempotency-auth-hardening.md) TC-AUTHH-02) |

---

### TC-USERS-03 — Admin-reset пароля и деактивация
**Предусловие:** есть непривилегированный пользователь.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Admin-reset пароля (`POST /auth/admin/users/:id/reset`) | Временный пароль выдан; у пользователя `password_change_required=true` |
| 2 | Вход этим пользователем | Редирект на `/change-password` (см. [`auth-flow.md`](auth-flow.md) TC-AUTH-07) |
| 3 | Деактивировать пользователя (`DELETE/PATCH /auth/admin/users/:id`) | Пользователь во вкладке «Деактивированные»; вход запрещён |

---

### TC-ROLES-01 — CRUD системных ролей
**Предусловие:** открыт `/admin/system-roles`.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Создать роль (Название*, Описание, набор Permissions) | `POST /api/v1/system-roles` → `201`, без ошибки CSRF (регресс BUG-F-010); роль в списке |
| 2 | Изменить permissions роли | `PATCH /api/v1/system-roles/{id}` → `200` |
| 3 | Удалить роль | `DELETE /api/v1/system-roles/{id}` → `200/204` |

---

### TC-ROLES-02 — Назначение/отзыв системной роли
**Предусловие:** есть пользователь и системная роль.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Назначить роль пользователю (`UserRolesModal`) | `POST /api/v1/users/{userId}/system-roles` → `200`; роль отражается |
| 2 | Отозвать роль | `DELETE /api/v1/users/{userId}/system-roles/{roleId}` → `200/204` |
| 3 | (опц.) Роль со сроком действия (`expires_at`) | Срок сохранён и отображается |

---

### TC-ROLES-03 — RBAC-запреты на админ-зону (негатив)
**Предусловие:** есть непривилегированный пользователь.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Войти обычным пользователем | В nav нет пунктов Пользователи/Аудит/Скрипты/Конфиги (гейт по permissions) |
| 2 | Открыть `/admin/users` напрямую по URL | «Нет доступа»/`403`, данные не отдаются |
| 3 | `POST /api/v1/system-roles` этим пользователем | `403 Missing permission` |

---

## Не покрыто / на доработку
- Проектные роли через UI (`/api/v1/projects/*/roles`) — см. [BUG-F-015](https://linear.app/lostpointer/issue/LOS-5/bug-f-015-rbac-project-roles-return-404-apiv1projectsroles-not-routed) (404 через прокси); enforcement — в [`rbac-permissions.md`](rbac-permissions.md).
- Пагинация больших списков пользователей.

> Статус: сценарии описаны; результаты прогона — в [`test-reports.md`](test-reports.md).
