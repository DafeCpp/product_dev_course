# Experiment Tracking Platform — статус и roadmap

Этот документ — **единый source of truth** по текущему прогрессу и планам развития платформы.

Он объединяет (и заменяет) два прежних документа:
- `projects/frontend/MVP_STATUS.md` (статус фронтенда для MVP)
- `docs/experiment-service-roadmap.md` (roadmap Experiment Service)

## Быстрые ссылки

- **ТЗ / требования**: `docs/experiment-tracking-ts.md`
- **Acceptance checklist**: `docs/mvp-acceptance-checklist.md`
- **Experiment Service DB schema**: `docs/experiment-service-db-schema.md`
- **ADR: TimescaleDB**: `docs/adr/002-timescaledb-telemetry.md`

---

## Frontend (Experiment Portal) — статус для MVP

### Что уже сделано ✅

#### 1) Базовая структура приложения
- ✅ React + TypeScript + Vite настроен
- ✅ React Router для навигации
- ✅ React Query для работы с API
- ✅ Axios клиент с interceptors для токенов
- ✅ Базовый Layout компонент

#### 2) CRUD экспериментов (Experiments)
- ✅ **Список экспериментов** (`/experiments`)
  - Фильтрация по project_id, status
  - Поиск по названию/описанию
  - Пагинация
  - Отображение статусов, тегов, метаданных
- ✅ **Создание эксперимента** (`/experiments/new`)
  - Форма с полями: project_id, name, description, experiment_type, tags, metadata
  - Валидация JSON для metadata
- ✅ **Детали эксперимента** (`/experiments/:id`)
  - Просмотр всей информации
- Редактирование (✅ реализовано: модальное окно `ExperimentEditModal`, PATCH + смена статуса/архивация)
- ✅ **Удаление эксперимента** (кнопка в деталях, `experimentsApi.delete`)
  - Список запусков эксперимента

#### 3) CRUD запусков (Runs)
- ✅ **Список запусков** (компонент `RunsList`)
  - Отображение в таблице
  - Статусы, параметры, длительность
- ✅ **Детали запуска** (`/runs/:id`)
  - Просмотр информации о запуске
  - Завершение запуска (complete)
  - Пометить как ошибка (fail)
  - Отображение параметров и метаданных
  - Управление capture sessions (старт/стоп отсчёта)
  - Отображение активной capture session
  - Список всех capture sessions

#### 4) API клиент
- ✅ Базовый API клиент (`projects/frontend/apps/experiment-portal/src/api/client.ts`)
- ✅ Методы для experiments: list, get, create, update, delete, search
- ✅ Методы для runs: list, get, create, update, complete, fail
- ✅ Методы для sensors: list, get, create, update, delete, rotateToken
- ✅ Методы для capture sessions: list, create, stop, delete
- ✅ Методы для webhooks и audit: webhooks (list, create, delete), deliveries (list, retry), run_events (list по run_id / session_id)
- ✅ Auth API клиент (`projects/frontend/apps/experiment-portal/src/api/auth.ts`) для работы с Auth Proxy
- ✅ Интеграция с Auth Proxy через HttpOnly cookies
- ✅ Автоматическое обновление токенов через interceptor
- ✅ Обработка 401 ошибок (автоматический refresh)

#### 5) Типы TypeScript
- ✅ Полный набор типов в `projects/frontend/apps/experiment-portal/src/types/index.ts`

#### 6) Телеметрия (SSE viewer)
- ✅ Просмотр live-стрима (SSE) в `TelemetryStreamModal` (с debug toast для ошибок)
- ✅ Страница `/telemetry`: мульти‑панельный просмотр графиков (`TelemetryViewer` + `TelemetryPanel`, Plotly; настройки/панели сохраняются в localStorage)

---

### Что нужно сделать (не MVP / backlog) ❌

#### Улучшения UI/UX
- **Loading states:** ✅ Улучшено: компонент `Loading` со спиннером и текстом; полноэкранная загрузка на списках экспериментов и датчиков (в т.ч. «Загрузка проектов...» до появления выбора); формы/модалки уже имели индикацию при submit (кнопка «Сохранение...», «Вход...» и т.д.).
- **Toast для успешных операций:** ✅ Расширено: тосты успеха уже были на создание/обновление/удаление экспериментов, запусков, датчиков, проектов, webhooks и т.д.; добавлен тост при успешном входе (Login).
- **Валидация форм на клиенте:** ✅ Базовая: обязательные поля (проект, название) и проверка JSON в формах экспериментов и запусков; metadata/params должны быть объектами (не массив), сообщения об ошибках через notifyError.
- **Телеметрия — управление capture_session:** ✅ Реализовано: на странице `/telemetry` при выбранном пуске (run) в блоке фильтров отображаются кнопки «Старт отсчёта» / «Остановить отсчёт» (для run в статусе draft/running); запросы run + experiment и список сессий загружаются при выборе пуска.
- **Просмотр исторических данных для пуска/отсчёта:** ✅ Реализовано: режим «history» в TelemetryViewer — выбор capture session, загрузка точек с пагинацией, Plotly-графики по сенсорам, фильтр сенсоров, raw/physical, include late data, лимит точек, asc/desc.

#### Мобильный клиент (Android) ❌

**Цель:** нативное Android-приложение для полевого использования — просмотр экспериментов, запусков и телеметрии (live + historical) на планшете/телефоне рядом со стендом.

**Случаи использования:**
- Инженер-испытатель наблюдает live-телеметрию на планшете, стоя у аэродинамической трубы (руки заняты, ноутбук неудобен).
- Быстрый просмотр результатов последнего запуска без доступа к ноутбуку.
- Push-уведомления при завершении запуска или при превышении порогов телеметрии.

**Минимальный scope (MVP):**
1. **Авторизация:** логин через Auth Proxy (те же JWT-куки или native token storage).
2. **Список экспериментов и запусков:** фильтрация по проекту, статусу; pull-to-refresh.
3. **Детали запуска:** параметры, статус, список capture sessions.
4. **Live-телеметрия:** SSE-стрим через `GET /api/v1/telemetry/stream`; графики (MPAndroidChart или аналог); переключение raw/physical.
5. **Historical mode:** просмотр capture session, пагинированная загрузка точек, агрегация 1m.

**Технологический выбор:**
- **Kotlin + Jetpack Compose** (нативный) или **React Native** (переиспользование типов/API-клиента с веба).
- Если React Native — можно переиспользовать `src/api/client.ts`, типы из `src/types/index.ts`, хуки React Query.

**Зависимости:**
- Auth Proxy должен корректно работать с мобильными клиентами (CORS origin, cookie policy для нативных приложений или переход на Bearer-token flow).
- SSE-стрим должен работать через мобильную сеть (reconnect, keep-alive).

**Критерии готовности:**
- [ ] Авторизация (логин/логаут) работает с мобильного устройства.
- [ ] Список экспериментов и запусков с фильтрацией.
- [ ] Live-график хотя бы для одного датчика.
- [ ] Historical просмотр capture session.

#### ~~Монитор датчиков (online/offline dashboard)~~ ✅ Реализовано

**Реализовано:**

1. **Backend — автоматический статус датчика:**
   - Вычисляемый `connection_status`: `online` (heartbeat < 30s), `delayed` (30s–5min), `offline` (> 5min).
   - `GET /api/v1/sensors/status-summary` — количество online/delayed/offline по проекту.

2. **Backend — heartbeat history:**
   - `GET /api/v1/sensors/{id}/heartbeat-history?last=60m` — массив timestamps для sparkline.

3. **Frontend — страница `/sensor-monitor`:**
   - Сетка карточек датчиков с цветовыми индикаторами (`StatusIndicator`: зелёный/жёлтый/красный).
   - `SensorStatusSummaryBar` — сводка online/delayed/offline.
   - Heartbeat sparklines на каждой карточке.
   - Автообновление (polling).
   - Фильтр по проекту, типу датчика, статусу.

4. **Frontend — правая панель в `/telemetry` (Live Monitor UX):** ❌ (backlog)
   - Мини-спарклайны для каждого подключённого датчика.
   - Индикатор задержки (latency).
   - Heartbeat indicator (пульсирующая точка).

**Критерии готовности:**
- [x] Карточки датчиков с цветовым статусом online/delayed/offline.
- [x] Sparkline heartbeat за последний час.
- [x] Автообновление без перезагрузки страницы.

#### ~~Таймлайн capture sessions поверх графиков~~ ✅ Реализовано

**Реализовано:**
- Компонент `CaptureSessionTimeline.tsx` — полосы сессий с цветом по статусу, tooltip с ordinal_number/длительностью/статусом.
- Синхронизация с Plotly через `plotly_relayout` event в `TelemetryViewer.tsx` — zoom/pan синхронизирует `timeRange` таймлайна.
- Работает в history-режиме.

#### Обратный отсчёт до автозавершения ❌

**Цель:** предотвращение «забытых» запусков — визуальный таймер и автоматическое завершение.

**Что нужно реализовать:**

1. **Backend — настройка auto-complete timeout:**
   - Поле `auto_complete_after_minutes` в `Run` или `Experiment` (nullable, default null = без лимита).
   - Background worker: если `Run.status = running` и `updated_at + auto_complete_after_minutes < now()` → перевести в `succeeded` (или `failed` по настройке).
   - Audit event: `run.auto_completed`.

2. **Frontend — визуальный обратный отсчёт:**
   - В `RunDetail`: таймер «Автозавершение через NN:NN» (обратный отсчёт).
   - Блокировка повторного нажатия «Complete» / «Fail» (debounce, disabled state на 2 сек после клика).
   - Предупреждение за 5 минут до автозавершения (toast).

**Зависимости:** background worker (✅ есть), статусная машина Run.

**Критерии готовности:**
- [ ] Настройка timeout при создании/редактировании запуска.
- [ ] Таймер обратного отсчёта в UI.
- [ ] Автоматическое завершение worker'ом.
- [ ] Audit event.

---

## Cross-cutting backlog (Frontend + BFF/Auth Proxy)

### 🔥 Нормальный вывод ошибок для дебага (toast снизу справа) — HIGH PRIORITY ✅

Цель: при ошибках показывать **детали запроса/ответа** и **trace-id/request-id**, чтобы быстро сопоставлять UI ↔ логи/трейсы.

- **UI:** всплывающее окно снизу справа (toast) с возможностью раскрыть “Details” и кнопкой копирования
- **Запрос (sanitized):** method + URL, query params, основные headers (без секретов), body (если есть; с маскированием токенов/паролей)
- **Ответ:** HTTP status + сырое тело ответа сервиса (если есть)
- **Корреляция:** `trace-id` / `request-id` (например, из `traceparent`, `x-trace-id`, `x-request-id`)

Почему это приоритет:
- снижает время диагностики “почему сломалось” без похода в DevTools/логи
- помогает сразу связать UI-ошибку с backend traces/logs через `trace-id`/`request-id`

Критерии готовности (минимум):
- toast появляется на **все** ошибки запросов (включая network/CORS/timeout)
- есть кнопка **Copy** (копирует единый текст-блок с request/response/correlation)
- секреты маскируются (tokens/passwords/cookies)

✅ **Реализовано** в `experiment-portal`: один тост снизу справа на каждую ошибку (axios + auth client); компонент `DebugErrorToast` с кнопками Details/Copy, санитизация headers/body в `utils/httpDebug.ts`; корреляция из `x-trace-id`, `x-request-id`, `traceparent`.

### SSE proxy + CSRF hardening (Auth Proxy) ✅

✅ Реализовано в `projects/frontend/apps/auth-proxy`:
- ✅ корректное SSE-проксирование (SSE hardening + заголовки `text/event-stream`, `X-Accel-Buffering: no`, `Cache-Control: no-cache`)
- ✅ CSRF защита (double-submit cookie `csrf_token` + проверка `Origin/Referer` + `X-CSRF-Token`; покрыто тестами)

---

## Auth Service — управление пользователями и доступом

### Текущее состояние

- ✅ **Регистрация и логин:** `POST /auth/register`, `POST /auth/login`, JWT (access + refresh токены), bcrypt.
- ✅ **Смена пароля** (с подтверждением старого): `POST /auth/change-password`.
- ✅ **Проекты и роли:** CRUD проектов, `project_members` с ролями `owner` / `editor` / `viewer`.
- ✅ **Logout / отзыв токенов:** `POST /auth/logout` отзывает refresh-токен (JWT blacklist через таблицу `revoked_tokens`).
- ✅ **Восстановление пароля (self-service):** `POST /auth/password-reset/request` → reset-токен в ответе (dev/учебный режим, без email); `POST /auth/password-reset/confirm` → новый JWT.
- ✅ **Принудительный сброс пароля (admin):** `POST /auth/admin/users/{user_id}/reset` — admin задаёт новый временный пароль, устанавливает `password_change_required = true`; требует поля `is_admin` (миграция `004_password_reset.sql`).
- ✅ **Инвайт-система:** `POST /auth/admin/invites` / `GET /auth/admin/invites` / `DELETE /auth/admin/invites/{token}` (только admin); конфигурационный флаг `REGISTRATION_MODE=open|invite`; при `invite` — `POST /auth/register` принимает обязательный `invite_token`, проверяет активность (не истёк, не использован), помечает `used_at`; миграция `005_invite_system.sql`.
- ✅ **Управление пользователями admin'ом (backend):** поле `is_active` в таблице `users` (миграция `006_user_is_active.sql`); логин и доступ по токену блокируется для деактивированных аккаунтов.
  - `GET /auth/admin/users?search=&is_active=` — список всех пользователей с фильтрацией.
  - `PATCH /auth/admin/users/{user_id}` — изменить `is_active` / `is_admin`; нельзя трогать собственный аккаунт; защита от разжалования/удаления последнего активного admin'а (409).
  - `DELETE /auth/admin/users/{user_id}` — удалить пользователя; те же защиты.
- ✅ **Ротация refresh-токенов:** таблица `refresh_token_families`, reuse detection (отзыв всей цепочки при повторном использовании), cleanup worker.
- ✅ **Принудительная смена пароля:** `pcr` claim в JWT, middleware блокирует все запросы кроме change-password, frontend redirect на `/change-password`.
- ✅ **Поиск пользователей:** `GET /api/v1/users/search?q=<query>` — prefix match по username, autocomplete в `ProjectMembersModal`.
- ✅ **Поиск и фильтрация проектов:** `GET /projects?search=&role=&limit=&offset=` с пагинацией.
- ✅ **Retention policy аудит-лога:** `AUDIT_RETENTION_DAYS=365`, background cleanup worker, индексы по `created_at`.
- ✅ **Audit-лог (Auth Service):** централизованный журнал действий пользователей.
  - `GET /api/v1/audit-log` — запрос событий с фильтрацией по actor, action, scope, target, диапазону дат; требует `audit.read`; пагинация (limit 1–500, default 50).
  - `POST /api/v1/internal/audit` — внутренний endpoint для отправки событий из других сервисов (без auth, IP-based control).
  - Auth-service автоматически пишет события при логине; Experiment Service посылает события через `AuditClient` (fire-and-forget).
  - Тесты: `tests/integration/test_api_audit.py`, `tests/unit/test_audit_service.py`.
- ✅ **Кастомные роли проекта:** `GET/POST /api/v1/projects/{project_id}/roles`, `GET/PATCH/DELETE /api/v1/projects/{project_id}/roles/{role_id}`; нельзя удалять встроенные роли; требует `project.roles.manage`.
- ⚠️ **Начальный admin:** hardcode убран (см. ниже), но ⚠️ если `ADMIN_BOOTSTRAP_SECRET` не задан — bootstrap endpoint возвращает 404.

---

### Backlog

#### ~~Bootstrap admin-пользователя при чистой установке~~ ✅ Реализовано

**Реализовано:**
- Хардкод убран: `001_initial_schema.sql` создаёт только схему без данных; миграция `007_remove_hardcoded_admin.sql` удаляет устаревшего дефолтного admin-пользователя.
- Env-переменная `ADMIN_BOOTSTRAP_SECRET` в `settings.py`; если не задана — endpoint возвращает 404.
- `POST /auth/admin/bootstrap` (открытый, одноразовый): принимает `secret`, `username`, `email`, `password`; проверяет секрет и что admin'ов ещё нет (`count_admins() == 0`); создаёт первого пользователя с `is_admin=true`; возвращает токены.
- Тесты: `test_bootstrap_admin_success`, `test_bootstrap_admin_wrong_secret`, `test_bootstrap_admin_disabled`, `test_bootstrap_admin_already_exists`, `test_bootstrap_admin_duplicate_username`, `test_bootstrap_admin_invalid_password`.

#### Сброс пароля пользователя ⚠️ Частично (SMTP — ❌, принудительная смена — ✅)

> Примечание: admin-сброс (`POST /auth/admin/users/{user_id}/reset`), self-service (`POST /auth/password-reset/request` + `/confirm`) и принудительная смена пароля (pcr claim + middleware + frontend) уже реализованы. Остаётся email-интеграция.

**Что уже работает:**
- ✅ Admin устанавливает новый временный пароль + `password_change_required = true`.
- ✅ Self-service: `POST /auth/password-reset/request` → reset-токен в ответе (без отправки на email — dev/учебный режим).

**Что остаётся (backlog):**

1. **SMTP-интеграция для восстановления пароля:**
   - `POST /auth/forgot-password` принимает `email`, генерирует reset-токен, отправляет email со ссылкой `https://app/reset-password?token=<token>`.
   - Настройки: `SMTP_HOST`, `SMTP_PORT`, `SMTP_USER`, `SMTP_PASSWORD`, `SMTP_FROM` в `settings.py`.
   - HTML-шаблон письма с ссылкой и TTL (1 час).
   - Rate limit на `forgot-password` (3 запроса в час на email).

2. **Frontend — страница `/reset-password`:**
   - Форма: новый пароль + подтверждение.
   - Валидация токена на клиенте (GET `/auth/password-reset/validate?token=`).
   - Redirect на логин после успеха.

3. **Принудительная смена пароля при первом логине:** ✅
   - Если `password_change_required = true` — JWT содержит `pcr` claim, middleware блокирует все запросы кроме change-password, frontend redirect на `/change-password`.

**Зависимости:** SMTP-сервер (для продакшена); текущая реализация reset-токенов (✅).

**Критерии готовности:**
- [ ] Email с reset-ссылкой отправляется при `POST /auth/forgot-password`.
- [ ] Страница `/reset-password` позволяет задать новый пароль по токену.
- [x] Принудительный redirect при `password_change_required`.

#### ~~Инвайт-система: закрытая регистрация~~ ✅ Реализовано

**Реализовано:**
- Таблица `invite_tokens` (миграция `005_invite_system.sql`): `id`, `token` (uuid), `created_by`, `email_hint`, `expires_at`, `used_at`, `used_by`.
- `POST /auth/admin/invites` (только admin) — создать инвайт с `email_hint` и `expires_in_hours` (1–8760); возвращает `InviteResponse` со всеми полями и флагом `is_active`.
- `GET /auth/admin/invites?active_only=true|false` — список инвайтов.
- `DELETE /auth/admin/invites/{token}` — отозвать неиспользованный инвайт; использованный → 409.
- `POST /auth/register` — опциональное поле `invite_token`; при `REGISTRATION_MODE=invite` токен обязателен, проверяется активность, помечается `used_at`.
- Флаг `REGISTRATION_MODE=open|invite` в settings (default `open` для dev/тестов).

#### Управление пользователями (admin UI)

- ✅ **Backend:** `GET /auth/admin/users`, `PATCH /auth/admin/users/{user_id}` (is_active / is_admin), `DELETE /auth/admin/users/{user_id}`; защита последнего admin'а; `is_active` блокирует логин и token lookup.
- ✅ **Frontend:** страница `/admin/users` с таблицей пользователей, кнопками инвайта и сброса пароля. Компонент `AdminUsers.tsx` с вкладками «Пользователи» (поиск, фильтр, activate/deactivate, toggle admin, reset password, delete) и «Инвайты» (создание, список, отзыв, копирование токена). Тесты: `AdminUsers.test.tsx`.

#### ~~Поиск и suggest пользователей при управлении командой проекта~~ ✅ Реализовано

**Реализовано:**

1. **Backend — `GET /api/v1/users/search?q=<query>`:**
   - Поиск по username (prefix match, `ILIKE 'query%'`).
   - Возвращает `[{id, username, email, is_active}]`, limit 10.
   - Доступен для авторизованных пользователей (не только admin).

2. **Frontend — autocomplete в `ProjectMembersModal`:**
   - Input с debounce → `GET /users/search?q=`.
   - Dropdown с username + email; клик → выбор пользователя.
   - Выбор роли (owner/editor/viewer) → `POST /projects/{id}/members`.

**Критерии готовности:**
- [x] `GET /api/v1/users/search?q=` возвращает пользователей по prefix.
- [x] Autocomplete в UI при добавлении участника в проект.
- [x] Отображаются username, а не UUID.

#### ~~Поиск и фильтрация проектов~~ ✅ Реализовано

**Реализовано:**

1. **Backend — `GET /projects?search=<query>&role=<owner|editor|viewer>&limit=&offset=`:**
   - Поиск по `name` (ILIKE `%query%`).
   - Фильтр по роли пользователя в проекте.
   - Пагинация (limit/offset).

2. **Frontend — компонент поиска на странице проектов:**
   - Input с debounce → фильтрация списка проектов.
   - Dropdown «Все роли» / «Owner» / «Editor» / «Viewer».

**Критерии готовности:**
- [x] Поиск проектов по названию через API.
- [x] Фильтрация по роли в UI.

#### ~~Ротация refresh-токенов с чёрным списком~~ ✅ Реализовано

**Реализовано:**

1. **Backend — refresh token rotation:**
   - `POST /auth/refresh` → проверяет текущий refresh-токен, добавляет его в `revoked_tokens`, выдаёт новый refresh-токен + новый access-токен.
   - Reuse detection: если refresh-токен уже в `revoked_tokens` → отзыв всей цепочки (family), возврат 401.
   - Таблица `refresh_token_families` (family_id, user_id, created_at) для группировки цепочки.

2. **Auth Proxy — обновление cookies:**
   - При `/auth/refresh` обновляются обе cookies (access + refresh).

3. **Cleanup:**
   - Background task: удаление записей из `revoked_tokens` старше `REFRESH_TOKEN_TTL`.

**Критерии готовности:**
- [x] Каждый refresh выдаёт новый refresh-токен и отзывает старый.
- [x] Reuse detection: повторное использование отозванного токена → отзыв всей цепочки.
- [x] Cleanup устаревших записей.

#### Опционально: отдельный сервис управления доступами ❌

Если платформа будет масштабироваться (несколько организаций, SSO, LDAP), стоит рассмотреть
выделение auth-service в полноценный Identity-сервис или интеграцию с Keycloak / Ory Hydra.
На текущем этапе достаточно описанных выше эндпоинтов в `auth-service`.

**Что может потребоваться:**
- **Multi-tenancy:** организации как уровень выше проектов; изоляция данных.
- **SSO/SAML/OIDC:** интеграция с корпоративными IdP (Google Workspace, Okta, AD FS).
- **LDAP/AD:** синхронизация пользователей из Active Directory.
- **Fine-grained permissions:** замена трёхролевой модели (owner/editor/viewer) на policy-based (ABAC/PBAC).
- **API keys / Service accounts:** для CI/CD и автоматизации (сейчас используются только sensor tokens).

**Решение:** интеграция с Keycloak (self-hosted) или Ory Hydra + Oathkeeper, либо облачное решение (Auth0, Clerk).

---

## Experiment Service — roadmap

Документ описывает независимый план развития Experiment Service в рамках платформы Experiment Tracking.
Фокус — на возможностях самого сервиса, API-контрактах и взаимодействии с соседними компонентами.

### Цели и принципы
- **Единый источник правды** для сущностей `Experiment`, `Run`, `CaptureSession`.
- **Прозрачные статусы** запусков с историей изменений и массовыми операциями.
- **Гибкая интеграция** с Telemetry Ingest, Metrics и Artifact сервисами через стабильные API.
- **Наблюдаемость и аудит**: трассировки, бизнес-метрики, события для внешних consumers.

### Метрики успеха
- P95 ответа API < 400 мс при 200 RPS.
- MTTR по инцидентам сервиса < 20 мин.
- ≥ 95% критичных операций покрыто интеграционными тестами.
- Zero-downtime миграции для всех новых схем.

### Текущее состояние (актуализируется)
- **✅ Завершено (Foundation):** блок Foundation полностью (миграции, CRUD для `Experiment/Run/CaptureSession`, idempotency, пагинация, OpenAPI, RBAC). Добавлены домены `Sensor` и `ConversionProfile`, статусные машины и покрытие тестами (`tests/test_api_*`). Множественные проекты для датчиков реализованы полностью (backend: таблица `sensor_projects` в миграции `001_initial_schema.sql`, API endpoints, тесты; frontend: UI для управления проектами в `SensorDetail.tsx`). UI для управления доступом к проектам реализован (`ProjectMembersModal` с тестами). Профиль пользователя реализован (`UserProfileModal` с тестами).
- **✅ Завершено (Runs & Capture Management):** batch-update статусов, `CaptureSession` (ordinal_number + статусы), bulk tagging, audit-log (Run/CaptureSession), webhooks (outbox + dispatcher), **backfill/late-data процесс** (API start/complete, привязка late-записей, UI). Остаётся: расширение доменных инвариантов ⚠️.
- **✅ Audit middleware (Experiment Service):** `AuditMiddleware` перехватывает успешные мутирующие запросы (POST/PATCH/PUT/DELETE) и асинхронно отправляет события в auth-service через `AuditClient`; маппинг route → action (например, `POST /experiments` → `experiment.create`); извлечение user_id, project_id, client IP из контекста запроса.
- **✅ Script Runner (Experiment Service + backend_common):** встроенный движок выполнения пользовательских скриптов (Python, Bash, JavaScript):
  - `backend_common/script_runner/executor.py` — запуск в изолированных subprocess (только `PARAM_*` и `PATH` env vars), timeout (SIGTERM → SIGKILL), захват stdout/stderr.
  - `backend_common/script_runner/consumer.py` — `ScriptConsumer` слушает RabbitMQ очереди `script.execute.<service>` / `script.cancel.<service>`, публикует результаты в обмен `script.status`.
  - `backend_common/script_runner/runner.py` — `ScriptRunner` объединяет Consumer + Executor как embeddable компонент для любого сервиса.
  - Experiment Service интегрирует `ScriptRunner` с graceful failure при недоступности RabbitMQ.
  - Тесты: `backend_common/tests/test_script_executor.py`, `test_script_runner.py`.
- **✅ RBAC v2 + интеграция (актуализировано 2026-03-30):** полная реализация — новая схема БД (`permissions`, `roles`, `role_permissions`, `user_system_roles`, `user_project_roles`), `PermissionService`, JWT с `sa`/`sys` claims, API endpoints (`/permissions`, `/system-roles`, `/projects/{pid}/roles`), auth-proxy с заголовками `X-User-Permissions`/`X-User-System-Permissions`/`X-User-Is-Superadmin`, Redis-кэш effective-permissions, `ensure_permission` в experiment-service. Подробности — `docs/tasks-rbac-scripts.md`.
- **✅ Script Service API (актуализировано 2026-03-30):** сервис `projects/backend/services/script-service/` реализован — CRUD скриптов, Execution API, RabbitMQ dispatcher, тесты. **⚠️ Отсутствует:** `git_client.py` — git-интеграция для валидации/загрузки скриптов из репозитория.
- **❌ Не реализовано / в backlog** (подробности — в секции «Нереализованные задачи» ниже):
  - Frontend RBAC: `usePermissions`, `PermissionGate`, страницы «Аудит» и «Скрипты» (фаза 6 в `docs/tasks-rbac-scripts.md`)
  - `git_client.py` для script_runner (script-service + backend_common)
  - SLO/SLI мониторинг
  - Chaos-тесты
  - ~~Metrics Service (run_metrics API)~~ ✅ Реализовано (POST/GET /runs/{id}/metrics, summary, step-bucketed aggregations)
  - ~~Artifact Service~~ ⚠️ CRUD API + approve endpoints реализованы (16 тестов), frontend (таблица, фильтр, add/delete/approve dialogs); S3/pre-signed URL — ❌
  - ~~Comparison Service~~ ✅ Реализовано (POST/GET /experiments/{id}/compare, multi-run metric comparison с auto-downsampling; frontend: чекбоксы runs, overlay Plotly charts, summary table)
  - Подписки на события (event subscriptions)
  - SLA ingest API: ⚠️ per-sensor REST rate limiter реализован (requests + readings per window, 429 с Retry-After); журнал ошибок датчика — ❌
  - Фикстуры и seed-данные для демо
  - ~~Prometheus метрики~~ ✅ Реализовано (GET /metrics на всех 3 сервисах, HTTP + бизнес-метрики); алёрты — ❌
  - Примечание: **REST ingest** реализован отдельным сервисом `telemetry-ingest-service` (`POST /api/v1/telemetry`).
  - Примечание: **WebSocket ingest** ✅ реализован в `telemetry-ingest-service` (`GET /api/v1/telemetry/ws`). Подробнее — см. раздел «WebSocket ingest» ниже.
  - Примечание: **SSE stream (MVP)** реализован в `telemetry-ingest-service` (`GET /api/v1/telemetry/stream`) и используется во фронтенде (например, `TelemetryStreamModal`, `/telemetry` через `TelemetryViewer`/`TelemetryPanel`).
  - Примечание: **SSE auth (MVP)**: `telemetry-ingest-service` принимает токен как sensor token, так и user JWT (через auth-proxy); для клиентов без возможности выставить заголовок поддержан query `access_token`.
  - Примечание: **MVP late-data политика**: данные после stop capture session не “приклеиваются” обратно к сессии (помечаются в `meta.__system` как late); ingest запрещён для `archived`.
  - В `experiment-service` публичной ручки `/api/v1/telemetry` больше нет; real-time режимы со стороны `experiment-service` и полноценная интеграция со сценариями backfill/реплея — в backlog.
- **Зависимости:** сервис собран на `aiohttp 3.10`, `asyncpg 0.31`, `pydantic-settings 2.4`, `structlog`, `opentelemetry-sdk`/`opentelemetry-instrumentation-aiohttp-server`; тестируется через `pytest`, `pytest-aiohttp`, `yandex-taxi-testsuite[postgresql]`; кодоген — `openapi-generator-cli 7.17`.

### WebSocket ingest (`GET /api/v1/telemetry/ws`) ✅

> **Статус:** ✅ Реализовано.
> **Цель:** низкоlatency ingest для высокочастотных источников (RC-машинка 500 Гц), где HTTP-overhead на каждый батч неприемлем.

#### Что реализовано

- ✅ **Endpoint:** `GET /api/v1/telemetry/ws?sensor_id=<uuid>` в `telemetry-ingest-service`.
- ✅ **Аутентификация до WebSocket upgrade:** токен проверяется как обычный HTTP-запрос → клиент получает `401` / `400` без открытия WS-соединения.
- ✅ **Два способа передать токен:** `Authorization: Bearer <token>` (для устройств) или `?token=` / `?access_token=` (для браузеров, которые не могут выставить заголовок).
- ✅ **Протокол** (JSON text frames):
  - **Клиент → сервер:** `{ "run_id": "...", "capture_session_id": "...", "meta": {}, "readings": [...], "seq": 42 }`
  - **Ack:** `{ "status": "accepted", "accepted": 5, "seq": 42 }`
  - **Recoverable error** (соединение живёт): `{ "status": "error", "code": "validation_error", "message": "..." }`
- ✅ **Recoverable ошибки** (invalid JSON, validation error, scope mismatch) — error-сообщение без закрытия соединения.
- ✅ **Fatal ошибки** (auth, internal) — WS закрывается с кодом `1008` / `1011`.
- ✅ **Та же бизнес-логика, что REST ingest:** sensor auth, run/capture scope resolution, auto-attach active capture session, late-data marking, conversion profiles.
- ✅ **Auth-proxy:** `websocket: true` уже стоит в `@fastify/http-proxy` для telemetry-маршрутов — проксирование работает без изменений.
- ✅ **Rate limiting (per sensor, fixed window):** `WsRateLimiter` с двумя счётчиками — `messages` (кадров/окно) и `readings` (точек/окно); recoverable ошибка `rate_limited` с `retry_after`; настраивается через env (`WS_RATE_LIMIT_MESSAGES_PER_WINDOW`, `WS_RATE_LIMIT_READINGS_PER_WINDOW`, `WS_RATE_LIMIT_WINDOW_SECONDS`). Defaults: 600 сообщений/сек, 60 000 точек/сек.
- ✅ **Тесты:** `tests/test_api_ws_ingest.py` — 8 интеграционных тестов (happy path, seq echo, multiple batches, 400/401 до upgrade, invalid JSON, validation error, Authorization header); `tests/test_ws_rate_limit.py` — 7 unit + 3 интеграционных теста.

#### Ключевые файлы

| Файл | Назначение |
|------|-----------|
| `telemetry-ingest-service/src/.../api/routes/ws_ingest.py` | WebSocket handler |
| `telemetry-ingest-service/src/.../domain/dto.py` | `WsIngestMessageDTO` (без `sensor_id`, он из URL; опциональный `seq`) |
| `telemetry-ingest-service/src/.../settings.py` | `ws_max_message_bytes` (default 1 MB) |
| `telemetry-ingest-service/tests/test_api_ws_ingest.py` | Интеграционные тесты |

#### Ограничения / backlog

- Максимальный размер одного сообщения: `ws_max_message_bytes` (1 MB, настраивается через env).
- Rate limiting ✅ реализован (`WsRateLimiter`, per-sensor fixed window).
- Нет server-push: сервер только отвечает на входящие батчи. Для просмотра телеметрии используйте SSE `GET /api/v1/telemetry/stream`.

---

## Script Service — выполнение пользовательских скриптов

### Текущее состояние

Новый микросервис `projects/backend/services/script-service/` для управления и выполнения пользовательских скриптов.

- ✅ **Доменные модели:** `Script` и `ScriptExecution` с сериализацией.
- ✅ **aiohttp-приложение:** инициализация БД, применение миграций, CORS.
- ✅ **Execution engine** в `backend_common/script_runner/`:
  - Изолированный запуск в subprocesses (только `PARAM_*` env vars).
  - Поддержка Python, Bash, JavaScript.
  - Таймаут с принудительным завершением (SIGTERM → SIGKILL).
  - Захват stdout, stderr, exit code.
- ✅ **RabbitMQ-интеграция:** `script.execute.<service>` / `script.cancel.<service>` очереди; публикация результатов в `script.status` exchange.
- ✅ **Тесты:** `test_script_executor.py` (Python/Bash execution, exit codes, timeout, parameter passing, env isolation), `test_script_runner.py` (execution handling, cancellation, status publishing).

### Backlog

- ~~**API эндпоинты Script Service**~~ ✅ Реализовано: CRUD скриптов, Execution API, RabbitMQ dispatcher, тесты.
- **git_client.py:** ❌ клонирование/fetch/checkout репозитория, валидация `git_ref`, защита от path traversal — отсутствует в `backend_common/script_runner/` и `script-service/`.
- **Frontend:** ❌ UI управления скриптами, страница «Скрипты» (`/admin/scripts`), просмотр результатов выполнения.
- **Sandbox для JavaScript:** изолированный runtime (Node.js child process или QuickJS).
- **Ресурсные лимиты:** CPU, memory, disk quota на выполнение.
- **Версионирование скриптов:** история изменений.

---

## Дорожная карта

### 1. Foundation (итерации 1‑2)
- **Доменные модели и CRUD:** ✅ Реализовано. Доменные модели `Experiment`, `Run`, `CaptureSession`, базовые CRUD-ручки c привязкой к проектам. Реализовано в `experiment-service/api/routes/experiments.py`, `runs.py`, `capture_sessions.py`.
- **Множественные проекты для датчиков:** ✅ Реализовано.
  - ✅ **Backend:** таблица `sensor_projects` в миграции `001_initial_schema.sql`, обновление модели `Sensor`, API endpoints, RBAC, тесты.
  - ✅ **Frontend:** методы в API клиенте и UI в `SensorDetail.tsx` (добавление/удаление проектов).
- **Фильтрация датчиков по доступным проектам:** ✅ Реализовано.
  - ✅ **Backend:** `GET /api/v1/sensors` поддерживает `project_id` (один проект) или без него — тогда experiment-service использует заголовок `X-Project-Ids` (список UUID от auth-proxy) и возвращает датчики по всем доступным проектам пользователя; при прямом вызове без заголовка — fallback на `active_project_id` или пустой список.
  - ✅ **Auth-proxy:** при `GET /api/v1/sensors` без `project_id` в query запрашивает у auth-service список проектов пользователя и передаёт их в заголовке `X-Project-Ids`.
  - ✅ **Frontend:** в `SensorsList.tsx` есть выпадающий список проектов и фильтрация через `project_id`; при выборе «все проекты» (или без выбора проекта) запрос идёт без `project_id` и пользователь видит датчики из всех своих проектов.
- **Множественные проекты при создании датчика:** ✅ Реализовано (multi-select; первый проект основной).
- **Валидация состояний и idempotency:** ✅ Реализовано.
- **RBAC-хуки:** ✅ Реализовано (owner/editor/viewer; project scoping).
- **Многоуровневый доступ к проектам:** ✅ Реализовано (project members + роли).
- **Ограничение создания экспериментов:** ✅ Реализовано (owner/editor).
- **Автозаполнение проекта при создании эксперимента:** ✅ Реализовано.
- **UI для управления доступом к проектам:** ✅ Реализовано (`ProjectMembersModal.tsx`).
- **Модальное окно для просмотра и редактирования проектов:** ✅ Реализовано (`ProjectModal`).
- **Профиль пользователя:** ✅ Реализовано (`UserProfileModal.tsx`).
- **Миграции:** ✅ Реализовано (apply on startup).
- **OpenAPI v1:** ✅ Реализовано.

### 2. Runs & Capture Management (итерации 3‑4)
- **Массовые операции:** ✅ Реализовано (`POST /api/v1/runs:batch-status`).
- **Bulk tagging:** ✅ Реализовано (`POST /api/v1/runs:bulk-tags`).
- **Расширенная сущность CaptureSession:** ✅ Реализовано (ordinal_number, статусы, связь с ingest).
- **Контроль доменных инвариантов:** ✅ Реализовано.
  - ✅ нельзя финализировать `Run`, если есть активные `CaptureSession`
  - ✅ нельзя удалить `Sensor`, если он участвует в активных `CaptureSession`
  - ✅ нельзя удалить активную `CaptureSession`
  - ✅ нельзя удалить `Run`, если есть активные `CaptureSession` (`RunService.delete_run` + новый эндпоинт `DELETE /api/v1/runs/{run_id}`)
  - ✅ нельзя удалить `Experiment`, если есть `Run` в статусе `running` (`ExperimentService.delete_experiment` + `RunRepository.has_running_runs_for_experiment`)
  - ✅ нельзя создать `CaptureSession` для `Run` в терминальном статусе (succeeded/failed/archived)
- **Webhook-триггеры:** ✅ Реализовано (outbox + background dispatcher + dedupe/retry; best-effort delivery).
- **Frontend:** ✅ UI для управления webhooks (список/создание/удаление подписок, просмотр delivery log с фильтрацией по статусу, ручной retry). Реализовано: страница `/webhooks` (`Webhooks.tsx`), навигация в sidebar.
- **Аудит-лог действий пользователей:** ✅ Реализовано для `Run`/`CaptureSession` (events + API для чтения).
- **Frontend:** ✅ UI для просмотра audit-log: компонент `AuditLog` (timeline с типами событий, actor, payload; пагинация; collapsible). Интегрирован в `RunDetail` (события запуска + события каждой capture session).
- **Догрузка данных после завершения (late/backfill ingest):** ✅ Реализовано.
  - ✅ **Backfill API:** `POST /api/v1/runs/{run_id}/capture-sessions/{session_id}/backfill/start` — переводит `succeeded` → `backfilling`; `POST .../backfill/complete` — привязывает late-записи (UPDATE `telemetry_records.capture_session_id` по `meta.__system.capture_session_id`) и возвращает `backfilling` → `succeeded`; ответ содержит `attached_records`.
  - ✅ **Audit + Webhooks:** события `capture_session.backfill_started` / `capture_session.backfill_completed` записываются в audit-log и диспатчатся через webhooks.
  - ✅ **Frontend:** кнопки «Начать догрузку» / «Завершить догрузку» в `TelemetryViewer` (history mode) рядом с выбором capture session.
  - ✅ **MVP-политика сохранена:** ingest разрешён для `succeeded/failed` (late data), запрещён для `archived`; во время backfilling данные привязываются как обычные (не late).

### 3. Data Integrity & Scaling (итерации 5‑6)
- **Фоновые задачи (worker):** ✅ Реализовано: in-process async worker (`background_tasks.py`), запускается вместе с aiohttp-сервером, выполняет периодический sweep (интервал `worker_interval_seconds`, по умолчанию 60 с):
  - ✅ Очистка idempotency-ключей старше `idempotency_ttl_hours` (48 ч)
  - ✅ Автостоп зависших capture sessions (`running`/`backfilling` дольше `stale_session_max_hours` — 24 ч) → `failed`
  - ✅ Восстановление застрявших webhook deliveries (`in_progress` дольше `webhook_stuck_minutes` — 10 мин) → `pending`
  - ✅ Очистка старых succeeded webhook deliveries (старше `webhook_succeeded_retention_days` — 30 дней)
- **Индексы и денормализации:** ✅ Частично (базовые индексы/GIN есть, но без целевых денормализаций и оптимизации под нагрузку)
- **TimescaleDB для телеметрии:** ✅ Реализовано (см. `docs/adr/002-timescaledb-telemetry.md`).
  - ✅ **Hypertable:** `telemetry_records` — hypertable с time partitioning по `timestamp` и space partitioning по `sensor_id` (8 партиций, chunk interval 1 день).
  - ✅ **Generated column `signal`:** extracted from `meta->>'signal'` для индексирования; индекс `telemetry_records_sensor_signal_ts_idx`.
  - ✅ **Compression:** `compress_segmentby = 'sensor_id, signal'`, `compress_orderby = 'timestamp DESC'`; автоматическое сжатие чанков старше 7 дней.
  - ✅ **Retention:** автоматическое удаление сырых точек старше 90 дней.
  - ✅ **Continuous aggregate `telemetry_1m`:** 1-минутный downsampling (avg/min/max по raw_value и physical_value, sample_count), группировка по `sensor_id`, `signal`, `capture_session_id`; refresh policy (start_offset 7d, end_offset 1m, каждую минуту). Миграция `002_continuous_aggregates.sql`.
  - ✅ **API:** `GET /api/v1/telemetry/aggregated` в telemetry-ingest-service — запрос из `telemetry_1m` с фильтрами `capture_session_id`, `sensor_id`, `signal`, `time_from`/`time_to`, `limit`, `order`.
  - ✅ **Frontend:** чекбокс «агрегация 1m» в TelemetryViewer (history mode); при включении данные загружаются из continuous aggregate и отображаются как avg-линия с min/max band (Plotly fill).
- **Синхронизация по времени между датчиками:** ❌

  **Цель:** корректное выравнивание timestamps от разных датчиков, работающих с разной частотой и возможными дрифтами часов.

  **Проблема:** датчики отправляют timestamps в своих локальных часах; при сравнении данных с нескольких датчиков на одном графике возможен сдвиг. Особенно актуально для ESP32-устройств без NTP.

  **Что нужно реализовать:**

  1. **Server-side timestamping (опционально):** если клиент не передаёт `timestamp` — проставлять серверное время при ingest.
  2. **Clock offset estimation:** при первом подключении датчика вычислять offset = `server_time - client_time` и сохранять; применять корректировку при записи.
  3. **NTP sync status:** поле `ntp_synced: bool` в metadata датчика; предупреждение в UI для датчиков без NTP.
  4. **Resampling/interpolation:** при наложении данных от разных датчиков на один график — интерполяция к общей временной сетке (frontend или backend API).

  **Зависимости:** telemetry-ingest-service, frontend TelemetryViewer.

- **Инварианты хранения:** ⚠️ Частично

  Есть DB-констрейнты, уникальности и ссылочная целостность. Не реализовано:

  1. **Retention policy для аудит-лога:** ✅ `AUDIT_RETENTION_DAYS=365` настроен, background cleanup worker работает в auth-service и experiment-service, индексы по `created_at` добавлены.
  2. **Дедупликация телеметрии:** ✅ уникальный индекс `(sensor_id, timestamp, signal)` + `ON CONFLICT DO NOTHING` — дубли при burst-отправке исключены.
  3. **Retention для `revoked_tokens`:** cleanup токенов старше TTL (частично реализовано в background worker, но без настройки TTL для revoked_tokens).
  4. **Soft-delete vs hard-delete:** определить политику для удалённых экспериментов/запусков (сейчас hard delete; для аудита может потребоваться soft-delete с `deleted_at`).

- **Песочница для нагрузочного тестирования:** ❌

  **Цель:** проверить метрики успеха (P95 < 400ms при 200 RPS, 200 одновременных датчиков, 5k точек/сек).

  **Что нужно реализовать:**

  1. **Инструмент:** Locust или k6 (предпочтительно k6 — нативная поддержка WebSocket, SSE, HTTP/2).
  2. **Сценарии:**
     - `ingest_load.js`: N виртуальных датчиков, каждый отправляет M readings/sec через REST и WebSocket.
     - `api_load.js`: CRUD-операции (создание экспериментов, запусков, запросы списков с фильтрами).
     - `sse_load.js`: N SSE-подключений, проверка latency доставки.
     - `export_load.js`: параллельный экспорт больших capture sessions.
  3. **Инфраструктура:**
     - Docker Compose profile `load-test` с k6 + Grafana k6 dashboard.
     - Seed-скрипт: создать N датчиков, M экспериментов с данными.
  4. **Метрики для проверки:**
     - P50/P95/P99 latency по endpoint.
     - Throughput (RPS, readings/sec).
     - Error rate (% 5xx).
     - DB connection pool utilization.
     - Memory/CPU потребление сервисов.

  **Зависимости:** Docker Compose, seed-данные, Grafana (✅ уже есть).

  **Критерии готовности:**
  - [ ] k6 сценарии для ingest, API CRUD, SSE, export.
  - [ ] Отчёт с P95 latency при целевой нагрузке.
  - [ ] Идентифицированные bottlenecks и рекомендации.
- **Контроль версий схем:** ✅ Реализовано (таблица `schema_migrations` + checksum, применение миграций на старте и через `bin/migrate.py`)

### 3.5 Схемы преобразования датчиков (Conversion Profiles)

> **Статус:** ✅ Реализовано (этапы 1–3).
> **Приоритет:** Высокий — ключевая фича ТЗ (раздел 6.2).

#### Что реализовано

- ✅ **DB-схема:** таблица `conversion_profiles`, FK `sensors.active_profile_id`, FK `telemetry_records.conversion_profile_id`, enum `telemetry_conversion_status`. Таблица `conversion_backfill_tasks` для фоновых задач пересчёта (миграция `003_conversion_backfill.sql`).
- ✅ **Backend (experiment-service):**
  - Repository: `ConversionProfileRepository` — CRUD, `publish_profile()` (atomic).
  - Service: `ConversionProfileService` — list, create, publish.
  - `BackfillTaskRepository` + `BackfillService` — создание/получение задач пересчёта.
  - API routes: профили (`POST/GET/publish`), backfill (`POST/GET /api/v1/sensors/{sensor_id}/backfill`).
  - `TelemetryService._apply_conversion()` делегирует в `backend_common.conversion.apply_conversion()`.
  - Background worker `conversion_backfill` — обрабатывает pending задачи батчами по 1000 записей.
- ✅ **Shared conversion module** (`backend_common/conversion.py`): три вида конверсии — `linear` (a·x+b), `polynomial` (Σcᵢ·xⁱ), `lookup_table` (линейная интерполяция + clamp).
- ✅ **Telemetry ingest (real-time):**
  - `ProfileCache` в telemetry-ingest-service — in-memory TTL-кэш (60 сек) для активных профилей.
  - `_bulk_insert()` автоматически применяет конверсию: `raw_only` → `converted` / `conversion_failed`.
  - Приоритет клиента: `physical_value` от клиента → `client_provided`.
- ✅ **Frontend:**
  - Типы: `ConversionProfile`, `ConversionProfilesListResponse`, `BackfillTask`, `BackfillTasksListResponse`.
  - API: `conversionProfilesApi` (list, create, publish), `backfillApi` (start, list, get).
  - `ConversionProfileCreateModal` — создание профиля с выбором kind, специализированными полями для каждого типа, live-калькулятор (предпросмотр).
  - `SensorDetail` — секция «Профили преобразования» (таблица, создание, публикация) + секция «Пересчёт данных» (запуск backfill, progress bar, автообновление).

#### Что нужно реализовать

##### Этап 1: Серверное преобразование при ingest (real-time)

**Цель:** при получении `raw_value` без `physical_value` telemetry-ingest-service автоматически применяет активный профиль датчика.

1. **Загрузка и кэширование профилей** в telemetry-ingest-service:
   - При старте/первом использовании: `SELECT cp.* FROM conversion_profiles cp JOIN sensors s ON s.active_profile_id = cp.id WHERE s.id = $1`.
   - In-memory кэш `{sensor_id → ConversionProfile}` с TTL (например, 60 сек) или инвалидацией через webhook/event при публикации нового профиля.
   - Fallback: если профиля нет → оставить `physical_value = NULL`, `conversion_status = 'raw_only'`.

2. **Применение преобразования** (в `_bulk_insert` перед INSERT):
   - Если `reading.physical_value IS NOT NULL` → `client_provided` (приоритет клиента).
   - Иначе если `active_profile` для этого `sensor_id`:
     - `kind = 'linear'`: `physical = payload.a * raw + payload.b`.
     - `kind = 'polynomial'`: `physical = Σ(payload.coefficients[i] * raw^i)`.
     - `kind = 'lookup_table'`: линейная интерполяция по `payload.table` (`[{raw, physical}, ...]`).
   - При успехе: `conversion_status = 'converted'`, `conversion_profile_id = profile.id`.
   - При ошибке: `conversion_status = 'conversion_failed'`, `physical_value = NULL`.

3. **Поддерживаемые виды (`kind`)**:

| Kind | Payload (JSONB) | Формула |
|------|----------------|---------|
| `linear` | `{"a": float, "b": float}` | `physical = a * raw + b` |
| `polynomial` | `{"coefficients": [c0, c1, c2, ...]}` | `physical = c0 + c1*x + c2*x² + ...` |
| `lookup_table` | `{"table": [{"raw": r, "physical": p}, ...]}` | Линейная интерполяция; экстраполяция за границами — clamp к крайним значениям |

4. **Невалидный payload** → `conversion_failed` + лог с деталями.

##### Этап 2: Frontend — управление профилями

1. **API client**: `conversionProfilesApi.list(sensorId)`, `.create(sensorId, data)`, `.publish(sensorId, profileId)`.
2. **Sensor Detail** — секция «Профили преобразования»:
   - Таблица: version, kind, status, valid_from/to, created_by, published_by.
   - Бейдж активного профиля.
   - Кнопка «Создать профиль» → модалка:
     - Выбор kind (linear / polynomial / lookup_table).
     - Редактор payload (JSON или специализированные поля для каждого kind).
     - Предпросмотр: ввести raw → показать physical (live-калькулятор).
   - Кнопка «Опубликовать» (owner only) → POST `.../publish`.
3. **CreateSensor** — добавить optional секцию «Начальный профиль преобразования» (kind + payload).

##### Этап 3: Backfill engine (пересчёт при смене профиля)

**Цель:** при публикации нового профиля пользователь может запустить пересчёт `physical_value` для исторических данных.

1. **API:**
   - `POST /api/v1/sensors/{sensor_id}/conversion-profiles/{profile_id}/backfill` — запускает пересчёт.
   - Body: `{"scope": "all" | "capture_sessions", "capture_session_ids": [...], "time_from": "...", "time_to": "..."}`.
   - Возвращает `backfill_task_id`.

2. **Хранение задач:**
   - Таблица `conversion_backfill_tasks` (id, sensor_id, profile_id, scope, status [pending/running/succeeded/failed], total_records, processed_records, error, created_at, finished_at).
   - Статус: `pending → running → succeeded | failed`.

3. **Выполнение:**
   - Background worker task: `conversion_backfill_worker`.
   - Читает `telemetry_records` батчами (ORDER BY timestamp ASC, LIMIT 1000).
   - Для каждой записи: загружает профиль, применяет конверсию, UPDATE `physical_value`, `conversion_profile_id`, `conversion_status`.
   - Сохраняет прогресс (processed_records) в таблицу задач.
   - **Идемпотентность:** повторный запуск продолжает с последней обработанной записи.
   - **Сохранение аудита:** старая `conversion_profile_id` логируется (или хранится в `meta.__system.prev_profile_id`).

4. **Frontend:**
   - После publish → предложить «Пересчитать historical данные?».
   - Диалог выбора scope (все данные / конкретные capture sessions).
   - Прогресс-бар (poll GET `/backfill-tasks/{id}`).

5. **Webhook/audit events:** `conversion_profile.backfill_started`, `conversion_profile.backfill_completed`.

##### Этап 4: Расширения (backlog) ❌

- **Scheduled profiles:** ✅ Реализовано
  - Scheduled profiles worker: auto-activation `draft` -> `active`, auto-archive `active` -> `archived`.
  - Background worker в `background_tasks.py` выполняет `scheduled_profile_activation`.
  - Audit event: `conversion_profile.auto_activated`.

- **Custom formula:** ❌
  - Пользовательское выражение (подмножество Python) для нестандартных формул: `sqrt(x) * 2.5 + log(x + 1)`.
  - **Реализация:** AST-парсинг (`ast.parse` + whitelist: `+, -, *, /, **, sqrt, log, abs, sin, cos, min, max`); без `import`, `exec`, `eval`, `__`-атрибутов. Или использовать `simpleeval` (pip).
  - **Новый kind:** `custom_formula` с `payload: {"expression": "sqrt(x) * 2.5", "variable": "x"}`.
  - **Backend:** добавить `custom_formula` в `backend_common/conversion.py`.
  - **Frontend:** textarea для ввода формулы + live-калькулятор + валидация (try-parse, показать ошибку).

- **A/B профили:** ❌
  - Два active профиля одновременно для одного датчика с split по `run_id` или `capture_session_id`.
  - **Цель:** сравнение формул преобразования на одних и тех же данных.
  - **Реализация:** таблица `profile_ab_tests` (id, sensor_id, profile_a_id, profile_b_id, split_strategy, started_at, ended_at); при ingest — применять оба профиля, записывать `physical_value_a` и `physical_value_b` (или два `telemetry_records` с разными `conversion_profile_id`).
  - **Frontend:** UI для создания A/B-теста, графики с наложением двух кривых.

- **Валидация payload при создании:** ✅ Реализовано
  - Pydantic discriminated union по `kind` (linear, polynomial, lookup_table).
  - `linear`: `{a: number, b: number}` — оба обязательны.
  - `polynomial`: `{coefficients: number[]}` — минимум 1 элемент.
  - `lookup_table`: `{table: [{raw: number, physical: number}, ...]}` — минимум 2 точки, отсортировано по `raw`.
  - Серверная валидация как safety net для frontend.

- **Batch ingest с конверсией:** ❌
  - При batch insert > 100 readings — параллельная конверсия с `asyncio.gather` или thread pool.
  - **Проблема:** сейчас конверсия происходит последовательно в `_bulk_insert`; для lookup_table с большим количеством точек это может быть bottleneck.
  - **Реализация:** `asyncio.to_thread` для CPU-bound конверсий; или предвычисление lookup_table в numpy-массив при загрузке профиля.

#### Зависимости

- Этап 1 зависит от: существующих API профилей в experiment-service (✅), загрузки профиля в telemetry-ingest-service.
- Этап 2 зависит от: существующих API (✅), нового API-клиента на фронте.
- Этап 3 зависит от: worker-инфраструктуры (✅ `BackgroundWorker`), новой таблицы `conversion_backfill_tasks`.

#### Критерии готовности (минимум для «реализовано»)

- [x] Ingest с `raw_value` без `physical_value` → сервер применяет active profile (linear/polynomial/lookup_table) → `physical_value` != NULL, `conversion_status = 'converted'`.
- [x] Frontend: список профилей в Sensor Detail, создание профиля (linear/polynomial/lookup_table), live-предпросмотр, публикация.
- [x] Backfill: запуск пересчёта через API, background worker обрабатывает батчами, прогресс виден в UI (progress bar, auto-refresh).
- [x] Тесты: unit-тесты для `backend_common.conversion` (linear, polynomial, lookup_table, edge cases).

### 4. Integrations & Collaboration (итерация 7)
- **Enforcement бизнес-политик:** ❌
- **Расширенные фильтры API:** ✅ Реализовано: `GET /api/v1/experiments` поддерживает `?status=`, `?tags=` (comma-separated, @> containment), `?created_after=`, `?created_before=` (ISO-8601); `GET /api/v1/experiments/{id}/runs` — те же фильтры; `GET /api/v1/sensors` — `?status=`, `?created_after=`, `?created_before=`; поиск по тексту — `GET /api/v1/experiments/search?q=`.
- **Экспорт данных:** ✅ Реализовано (CSV/JSON + ZIP+Parquet).
  - ✅ **Метаданные:** `GET /api/v1/experiments/export?format=csv|json` и `GET /api/v1/experiments/{id}/runs/export?format=csv|json` с поддержкой фильтров (status, tags, created_after, created_before); до 5000 записей; на фронтенде кнопки «CSV» / «JSON» на списке экспериментов и списке запусков.
  - ✅ **Экспорт с данными датчиков (телеметрия) — Этап 1+2:** Backend API + Frontend UI для экспорта telemetry readings из capture sessions. Подробный план и статус ниже.
- **Подписки на события:** ❌

### 4.5 Экспорт данных с телеметрией датчиков

> **Статус:** ✅ Этапы 1, 2 и 3 (частично) реализованы (backend API + frontend UI + ZIP+Parquet экспорт). Остаток этапа 3 (асинхронный экспорт, шаблоны) — backlog.
> **Приоритет:** Средний — важно для анализа данных вне платформы (Jupyter, MATLAB, Excel).

#### Текущее состояние

Реализованы два API-эндпоинта для экспорта телеметрии и кнопки экспорта в RunDetail.

#### Что нужно реализовать

##### Этап 1: Backend — API экспорта телеметрии ✅

**Цель:** API для выгрузки readings привязанных к конкретным capture sessions / runs.

**Реализовано в** `experiment_service/api/routes/telemetry_export.py`.

1. **Экспорт по capture session:** ✅
   - `GET /api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export`
   - Query params: `format=csv|json`, `sensor_id` (опционально, фильтр), `signal` (опционально), `include_late=true|false`, `raw_or_physical=raw|physical|both`.
   - CSV-формат: `timestamp, sensor_id, signal, raw_value, physical_value, conversion_status, capture_session_id`.
   - ✅ Стриминговая отдача: `web.StreamResponse` + `conn.cursor(prefetch=5000)` — данные пишутся батчами по 5 000 строк, без in-memory накопления и без лимита на число записей.

2. **Экспорт по run (все capture sessions):** ✅
   - `GET /api/v1/runs/{run_id}/telemetry/export`
   - Аналогичные query params + `capture_session_id` (опционально, фильтр).
   - CSV добавляет колонку `capture_session_id`.

3. **Агрегированный экспорт:** ✅
   - `GET /api/v1/runs/{run_id}/telemetry/export?aggregation=1m` — использует continuous aggregate `telemetry_1m`.
   - CSV-формат: `bucket, sensor_id, signal, capture_session_id, sample_count, avg_raw, min_raw, max_raw, avg_physical, min_physical, max_physical`.

4. **Ограничения и безопасность:** ✅ (частично)
   - ✅ Лимит снят: streaming cursor, без in-memory накопления.
   - ✅ RBAC: доступ к данным только для участников проекта (через `resolve_project_id` + `ensure_project_access`).
   - ✅ Rate limit: реализован в `experiment-service` (`ExportRateLimiter`, per-user fixed window, default 10 req/60s).

##### Этап 2: Frontend — UI экспорта телеметрии ✅ (базовый)

1. **RunDetail / capture session list:** ✅
   - Кнопка «Экспорт телеметрии» рядом с каждой capture session → скачать CSV readings этой сессии.
   - Кнопка «Экспорт всей телеметрии» на уровне run → скачать readings всех capture sessions.

2. **Диалог настроек экспорта:** ✅ Реализовано.
   - Компонент `TelemetryExportModal`: формат (CSV/JSON), значения (raw/physical/both), агрегация 1m, include_late, фильтр по датчику и сигналу, фильтр по сессии (для экспорта всего run).
   - Интегрирован в `RunDetail` — кнопки «Экспорт телеметрии…» у каждой сессии и для всего run.

3. **TelemetryViewer (history mode):** ✅ Реализовано.
   - Кнопка «Экспорт данных…» открывает `TelemetryExportModal`, предзаполненный текущим состоянием (сессия, датчик, raw/physical, include_late, агрегация).

##### Этап 3: Расширенный экспорт ✅ (частично)

- ✅ **ZIP+Parquet экспорт:** `GET /experiments/{id}/export?format=zip` — полный экспорт эксперимента одним архивом (ZIP): метаданные эксперимента + все runs + все capture sessions + все telemetry readings в формате Parquet.
- **Асинхронный экспорт:** ❌ для больших объёмов (>100k записей) — создание задачи, фоновая генерация файла, уведомление о готовности, ссылка на скачивание.
- **Шаблоны экспорта:** ❌ пользователь может сохранить конфигурацию экспорта (фильтры, колонки, формат) и переиспользовать.

#### Зависимости

- Этап 1 зависит от: доступа к `telemetry_records` из experiment-service или inter-service API к telemetry-ingest-service; continuous aggregate `telemetry_1m` (✅).
- Этап 2 зависит от: API этапа 1, текущего UI RunDetail/TelemetryViewer.
- Этап 3 зависит от: worker-инфраструктуры (✅ `BackgroundWorker`), object storage для файлов.

#### Критерии готовности (минимум)

- [x] API: экспорт readings по capture session в CSV с фильтрами sensor_id, signal, raw/physical, include_late, aggregation.
- [x] API: экспорт readings по run (все capture sessions) в CSV/JSON.
- [x] Frontend: кнопка экспорта в RunDetail, скачивание файла.
- [x] Стриминг: экспорт 50k+ записей без OOM — реализовано через `StreamResponse` + cursor.
- [x] Тесты: integration (создать session -> ingest data -> export -> verify CSV).

### 5. Hardening & Launch (итерация 8+)
- **SLO/SLI мониторинг:** ❌
- **Prometheus метрики:** ✅ Реализовано: `GET /metrics` на всех 3 сервисах (auth-service, experiment-service, telemetry-ingest-service); HTTP метрики (requests total, latency histogram, error rate) + бизнес-метрики (experiments count, active runs, ingest throughput и т.д.).
- **Трассировка (OpenTelemetry):** ✅ Реализовано: `TracerProvider` + OTLP HTTP exporter + авто-инструментализация aiohttp-server (`opentelemetry-instrumentation-aiohttp-server`); активируется при наличии `OTEL_EXPORTER_ENDPOINT`; `get_tracer()` доступен для ручных спанов в сервисном/репозиторном коде; graceful shutdown с flush спанов.
- **Chaos-тесты и отказоустойчивость:** ❌
- **Telemetry ingest disk spool:** ✅ Реализовано (см. раздел «WebSocket ingest» выше для контекста; детали ниже).
- **Документация:** ✅ (индекс `docs/README.md`, quickstart `docs/local-dev-docker-setup.md`, demo `docs/demo-flow.md`, E2E чеклист `docs/manual-testing.md`, дебаг `docs/ui-debugging.md`)

