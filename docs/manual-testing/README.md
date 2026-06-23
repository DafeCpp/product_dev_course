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
| [`test-reports.md`](test-reports.md) | Журнал прохождений: дата, окружение, результаты, найденные баги | Отчёты |

Найденные при прогоне баги фиксируются в [`../bugs.md`](../bugs.md).

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

Префиксы ID: `TC-AUTH-*` (аутентификация), `TC-NAV-*` (навигация/smoke), `TC-CRUD-*` (CRUD/lifecycle), `TC-RBAC-*` (роли/доступ), `TC-IDEM-*`/`TC-AUTHH-*` (идемпотентность/hardening), `TC-TELE-*` (телеметрия), `TC-E2E-*` (сквозные).

## История прогонов

Отчёты о прохождениях — в [`test-reports.md`](test-reports.md) (новые сверху). Последний: **2026-06-10** — auth-флоу (OK) + smoke-навигация, найдены [BUG-F-013](../bugs.md), [BUG-F-014](../bugs.md).
