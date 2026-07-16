# Ручное тестирование — индекс сценариев

Каталог сценариев ручного (E2E / smoke) тестирования платформы. Каждый файл — набор переиспользуемых тест-кейсов с шагами, ожидаемым и фактическим результатом.

## Сценарии

| Файл | Что покрывает | Тип |
|------|---------------|-----|
| [`e2e-happy-path.md`](e2e-happy-path.md) | Полный сквозной путь: логин → project → sensor → experiment → run → capture session → телеметрия → проверка в UI и БД | E2E |
| [`auth-flow.md`](auth-flow.md) | Аутентификация: логин (валидный/невалидный), логаут, защита роутов, страницы register/forgot-password | Функциональный |
| [`smoke-navigation.md`](smoke-navigation.md) | Загрузка всех разделов портала без ошибок (HTTP-статусы, консоль) | Smoke |
| [`crud-lifecycle.md`](crud-lifecycle.md) | CRUD и state machine: project → experiment → run → capture session, переходы статусов, удаление | Функциональный |
| [`rbac-permissions.md`](rbac-permissions.md) | Роли owner/editor/viewer, инвайты, выдача/отзыв ролей, изоляция проектов (IDOR) | Функциональный |
| [`idempotency-auth-hardening.md`](idempotency-auth-hardening.md) | `Idempotency-Key` (replay/409/race), single-use инвайты, admin-reset, парсинг токенов | Функциональный |
| [`telemetry-e2e.md`](telemetry-e2e.md) | Датчик → токен → ingest → конверсия raw→physical → UI/БД, heartbeat, backfill | E2E |
| [`sensors.md`](sensors.md) | Датчики: список, регистрация, токен/ротация, error-log, мульти-проект, монитор heartbeat | Функциональный |
| [`telemetry-viewer.md`](telemetry-viewer.md) | Telemetry Viewer: live (SSE), history, фильтры, экспорт, пустые/ошибочные данные | Функциональный |
| [`experiment-comparison.md`](experiment-comparison.md) | Сравнение runs: выбор, метрики, экспорт CSV/JSON, граничные случаи | Функциональный |
| [`webhooks.md`](webhooks.md) | Webhooks: CRUD подписок, доставки, retry, фильтрация по проекту | Функциональный |
| [`admin-users-roles.md`](admin-users-roles.md) | Admin: пользователи, инвайты, admin-reset, системные роли, RBAC-запреты | Функциональный |
| [`audit-log.md`](audit-log.md) | Аудит-лог: журнал, запись событий, фильтры, RBAC | Функциональный |
| [`admin-scripts.md`](admin-scripts.md) | Admin Scripts: реестр, запуск, история/детали выполнения, отмена, RBAC | Функциональный |
| [`admin-configs.md`](admin-configs.md) | Admin Configs: CRUD, activate/rollback, история, optimistic locking, sensitive values | Функциональный |
| [`rate-limits-qos.md`](rate-limits-qos.md) | Rate Limits & QoS: формы auth/experiment/telemetry, сохранение, валидация | Функциональный |
| [`test-reports.md`](test-reports.md) | Журнал прохождений: дата, окружение, результаты, найденные баги | Отчёты |

Найденные при прогоне баги фиксируются в [Linear](https://linear.app/lostpointer).

Автоматизированный smoke/happy-path (login + полный lifecycle) вынесен в **Cypress**:
`cd projects/frontend/apps/experiment-portal && npm run e2e` (см. `cypress/e2e/*.cy.ts`). Этот каталог —
про **ручной** прогон (в браузере или через Playwright MCP), сценарии дополняют Cypress, не заменяют его.

## Предусловия (общие)

- Поднятый dev-стек: `make dev-up` (см. [`../local-dev-docker-setup.md`](../local-dev-docker-setup.md)).
- Доступен Portal: `http://localhost:3000`.
- Dev-учётка администратора: `admin` / `Admin123` (из `Makefile`, цель `make dev-init` создаёт первого админа; пароль задаётся переменной `DEV_ADMIN_PASSWORD`).

## Инструментарий

Сценарии прогоняются вручную в браузере **или** автоматизированно через **Playwright MCP** (headless Chrome).
При прогоне через Playwright локально:

```bash
# одноразовая установка браузера (MCP-сервер ожидает канал "chrome")
npx playwright install chrome
```

Дальше браузером управляет агент (навигация, заполнение форм, снимки accessibility-дерева, чтение консоли).

## Формат тест-кейса

Каждый сценарий описывается единообразно:

```markdown
### TC-XXX-NN — Краткое название
**Предусловие:** состояние перед началом (залогинен/нет, есть проект и т.п.)

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | … | … |
| 2 | … | … |

**Факт (дата):** что наблюдалось при последнем прогоне. ✅ / ❌ (ссылка на баг).
```

Префиксы ID: `TC-AUTH-*` (аутентификация), `TC-NAV-*` (навигация/smoke), `TC-CRUD-*` (CRUD/lifecycle), `TC-RBAC-*` (роли/доступ), `TC-IDEM-*`/`TC-AUTHH-*` (идемпотентность/hardening), `TC-TELE-*` (телеметрия — ingest и viewer), `TC-E2E-*` (сквозные), `TC-SENSOR-*` (датчики), `TC-CMP-*` (сравнение runs), `TC-WHK-*` (webhooks), `TC-USERS-*`/`TC-ROLES-*` (пользователи/системные роли), `TC-AUDIT-*` (аудит), `TC-SCRIPT-*` (скрипты), `TC-CONFIG-*` (конфиги), `TC-QOS-*` (rate limits / QoS).

## Покрытие роутов Portal

Каждый роут из `projects/frontend/apps/experiment-portal/src/App.tsx` должен иметь сценарий
либо явную пометку-исключение. Актуальная карта:

| Роут (App.tsx) | Сценарий |
|----------------|----------|
| `/login` | [`auth-flow.md`](auth-flow.md) TC-AUTH-01…04 |
| `/register` | [`auth-flow.md`](auth-flow.md) TC-AUTH-05, TC-AUTH-08 |
| `/forgot-password` | [`auth-flow.md`](auth-flow.md) TC-AUTH-06, TC-AUTH-09 |
| `/reset-password` | [`auth-flow.md`](auth-flow.md) TC-AUTH-09 |
| `/change-password` | [`auth-flow.md`](auth-flow.md) TC-AUTH-07 |
| `/` → `/experiments` | [`smoke-navigation.md`](smoke-navigation.md) TC-NAV-01/02 |
| `/projects` | [`crud-lifecycle.md`](crud-lifecycle.md); прямой заход — [`smoke-navigation.md`](smoke-navigation.md) (BUG-F-013) |
| `/experiments` | [`crud-lifecycle.md`](crud-lifecycle.md) |
| `/experiments/:id` | [`crud-lifecycle.md`](crud-lifecycle.md) |
| `/experiments/:experimentId/compare` | [`experiment-comparison.md`](experiment-comparison.md) |
| `/runs/:id` | [`crud-lifecycle.md`](crud-lifecycle.md) TC-CRUD-04/05 |
| `/sensors`, `/sensors/new`, `/sensors/:id` | [`sensors.md`](sensors.md) |
| `/sensor-monitor` | [`sensors.md`](sensors.md) TC-SENSOR-07 |
| `/telemetry` | [`telemetry-viewer.md`](telemetry-viewer.md) (+ [`telemetry-e2e.md`](telemetry-e2e.md)) |
| `/webhooks` | [`webhooks.md`](webhooks.md) |
| `/admin/users` | [`admin-users-roles.md`](admin-users-roles.md) TC-USERS-* |
| `/admin/system-roles` | [`admin-users-roles.md`](admin-users-roles.md) TC-ROLES-* |
| `/admin/audit` | [`audit-log.md`](audit-log.md) |
| `/admin/scripts` | [`admin-scripts.md`](admin-scripts.md) |
| `/admin/configs` | [`admin-configs.md`](admin-configs.md) |
| `/admin/rate-limits` | [`rate-limits-qos.md`](rate-limits-qos.md) |

Роуты создания `CreateExperiment`/`CreateProject` не смонтированы в `App.tsx` (создание идёт через
модалки) — **исключение**, покрывается в [`crud-lifecycle.md`](crud-lifecycle.md).

## История прогонов

Отчёты о прохождениях — в [`test-reports.md`](test-reports.md) (новые сверху). Последний: **2026-07-16** — сквозной happy path + smoke по всем новым разделам (LOS-182), найдены [LOS-190](https://linear.app/lostpointer/issue/LOS-190) (audit-writes 404), [LOS-191](https://linear.app/lostpointer/issue/LOS-191) (System Roles Permissions пустые), [LOS-192](https://linear.app/lostpointer/issue/LOS-192) (stale project_id после logout), [LOS-193](https://linear.app/lostpointer/issue/LOS-193) (смена пароля не работает — `404`). Предыдущий: 2026-06-10 — auth-флоу (OK) + smoke-навигация, [BUG-F-013](https://linear.app/lostpointer/issue/LOS-6/bug-f-013-projects-page-returns-http-500-on-direct-load-f5-reload) (закрыт, не воспроизводится), [BUG-F-014](https://linear.app/lostpointer/issue/LOS-7/bug-f-014-webhooks-page-sends-requests-without-project-id-400-bad).
