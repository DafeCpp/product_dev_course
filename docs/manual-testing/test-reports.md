# Отчёты о прогонах ручного тестирования

Журнал прохождений сценариев из этой папки. Новые отчёты добавляются **сверху** (свежие первыми). Определения сценариев — в [`auth-flow.md`](auth-flow.md), [`smoke-navigation.md`](smoke-navigation.md), [`e2e-happy-path.md`](e2e-happy-path.md). Баги — в [Linear](https://linear.app/lostpointer).

Шаблон отчёта — в конце файла.

---

## 2026-07-16 — LOS-182: расширение каталога + сквозной прогон (happy path, RBAC, Configs, auth-циклы)

| | |
|---|---|
| **Тестировщик** | Claude Code (агент) |
| **Инструмент** | Playwright MCP (headless Chrome) + curl (auth-proxy сессия, telemetry ingest, config-service optimistic locking) + psql (проверка в БД) |
| **Ветка** | `lostpointmeister/los-182-teste2e-portal` |
| **Окружение** | Локальный dev-стек (`make dev-up`); по ходу пересобраны образы `experiment-service` и `experiment-portal` (устарели после LOS-86/90/96/97 — см. «Замечания») |
| **Учётка** | `admin`/`Admin123` (superadmin) + 2 новых тестовых пользователя (`qa_editor` — открытая регистрация, `qa_viewer` — по инвайту, роль `config_viewer`) |
| **Объём** | Happy path project→experiment→run→capture session→sensor→telemetry; smoke по всем разделам; TC-AUTH-02/07/08/09; TC-USERS-01/02/03; TC-ROLES-02/03; TC-CONFIG-01/02/05/06; TC-WHK-01; TC-CMP-01/04; TC-SCRIPT-01; TC-QOS-01; TC-AUDIT-01/02; TC-SENSOR-01/02 |

### Результаты

| Сценарий | Итог |
|----------|------|
| Happy path: project→experiment→run→capture session→sensor→telemetry | ✅ Весь цикл через UI, включая нативные `window.prompt`/`window.confirm`; heartbeat датчика обновился |
| TC-AUTH-02 Валидный логин | ✅ |
| TC-AUTH-07 Смена пароля (`password_change_required`) | ❌ `POST /auth/change-password` → `404`; пользователь застревает → [LOS-193](https://linear.app/lostpointer/issue/LOS-193) |
| TC-AUTH-08 Регистрация (открытая + по инвайту) | ✅ Оба режима работают; single-use инвайта подтверждён (`401` на повтор) |
| TC-AUTH-09 Сброс пароля по ссылке | ✅ Полный цикл request→confirm→login; повторное использование токена → `401` |
| TC-USERS-01/02 Список + инвайты/регистрация | ✅ |
| TC-USERS-03 Admin-reset (шаги 1-2) | ✅ (шаг 2 зависит от TC-AUTH-07, который сломан — см. выше); шаг 3 (деактивация) **не прогонялся** |
| TC-ROLES-02 Назначение системной роли | ✅ `config_viewer` назначена, эффективное разрешение подтверждено функционально |
| TC-ROLES-03 RBAC-запреты (негатив) | ✅ Nav скрывает админ-пункты; прямой заход → `403` |
| TC-CONFIG-01/02 Список + создание | ✅ |
| TC-CONFIG-05 Optimistic locking | ✅ Устаревший `If-Match`/version → `412` |
| TC-CONFIG-06 Sensitive-масскирование | ✅ `config_viewer` (без `configs.sensitive.read`) видит `"value":"***"` и в list, и в history |
| TC-WHK-01 Webhooks (с проектом / без проектов) | ✅ / ⚠️ на пустом воркспейсе — спурьезные `403` от stale `project_id` → [LOS-192](https://linear.app/lostpointer/issue/LOS-192) |
| TC-CMP-01/04 Comparison page | ✅ |
| TC-SCRIPT-01, TC-QOS-01 Smoke | ✅ |
| TC-AUDIT-01/02 Журнал + запись событий | ❌ Журнал пустой после успешных create-действий → [LOS-190](https://linear.app/lostpointer/issue/LOS-190) |
| TC-SENSOR-01/02 Список + регистрация + токен | ✅ |
| `/projects` прямой заход (BUG-F-013) | ✅ Не воспроизводится |
| `/admin/system-roles` консоль | ❌ Пустая колонка Permissions + key-prop warning → [LOS-191](https://linear.app/lostpointer/issue/LOS-191) |

**Итого:** happy path и большинство smoke-проверок — зелёные. Найдено **4 новых бага**, один из них (LOS-193, смена пароля) — блокирующий для полноценного onboarding пользователей.

### Найденные баги

| ID | Severity | Кратко |
|----|----------|--------|
| [LOS-193](https://linear.app/lostpointer/issue/LOS-193) | High | `POST /auth/change-password` не проксируется auth-proxy (`404`) — смена пароля полностью нерабочая, пользователь после admin-reset застревает без выхода. |
| [LOS-190](https://linear.app/lostpointer/issue/LOS-190) | High | Audit-записи от experiment-service (`experiment.create`, `run.create`, ...) получают `404` при отправке в auth-service — аудит-лог тихо остаётся пустым. Причина: `.env` `AUTH_SERVICE_URL` без `/api/v1`. |
| [LOS-191](https://linear.app/lostpointer/issue/LOS-191) | Medium | `/admin/system-roles`: колонка Permissions пустая для всех ролей — API отдаёт `permissions` как `string[]`, компонент ждёт `{id,name}[]`. |
| [LOS-192](https://linear.app/lostpointer/issue/LOS-192) | Low | `active_project_id` в localStorage не чистится при logout — у следующего пользователя на том же браузере уходят спурьезные запросы со старым `project_id` (`403` + шумные тосты). |

### Замечания / инфраструктура (не баги продукта)

При первом заходе `/runs/:id`, `/sensors`, `/telemetry` падали с `500` (`Failed to resolve import "frontend-common"`) — образ `experiment-portal` и anonymous-volume `/app/node_modules` были собраны до появления пакета `frontend-common` (LOS-96/97); аналогично `experiment-service` падал на старте (`ModuleNotFoundError: backend_common.otel`, до консолидации LOS-86/90). Оба пересобраны (`docker compose build`; для portal ещё и `--force-recreate --renew-anon-volumes`, т.к. простого rebuild оказалось недостаточно — старый `node_modules` жил в anonymous volume) — после этого все страницы отрисовались штатно. Локальный дев-стек может расходиться с `develop` после подобных рефакторов backend-common/frontend-common; обновление образов не тривиально из-за anonymous volumes в `docker-compose.yml`.

### Замечания / не покрыто

- Webhooks CRUD (создание/удаление подписки, retry доставки) — тестировался только список/фильтрация, не сама мутация.
- Configs: activate/deactivate (TC-CONFIG-03), rollback (TC-CONFIG-04), RBAC-негативы на мутации (TC-CONFIG-07) — не прогонялись.
- Admin Scripts/Rate Limits — только smoke открытия раздела, без CRUD/сохранения значений.
- Полный сквозной happy-path с телеметрией через Live SSE/history/export (`telemetry-viewer.md` TC-TELE-08…11) — не прогонялся отдельно от ingest-проверки.
- RBAC-негативы для Configs/Webhooks с ролью `config_editor`/`config_operator` (частичные права) — проверялась только `config_viewer` (read-only).

---

## 2026-06-10 — RBAC / роли доступа (TC-RBAC)

| | |
|---|---|
| **Тестировщик** | Claude Code (агент) |
| **Инструмент** | curl через auth-proxy (enforcement) + auth-service напрямую (выдача ролей, т.к. прокси 404) |
| **Ветка** | `fix/backend-idempotency-and-auth-hardening` |
| **Окружение** | Локальный dev-стек |
| **Setup** | Пользователи `editor_user`, `viewer_user` (регистрация); роли выданы прямо в auth-service. Проект A = `QA Idempotency Run`, проект B = `RBAC Project B` |
| **Объём** | `rbac-permissions.md` TC-RBAC-02, 03, 04, 05 |

### Результаты

| Сценарий | Итог |
|----------|------|
| TC-RBAC-02 editor: права | ✅ Создаёт эксперимент (`201`); выдать себе owner → `403 project.members.change_role` |
| TC-RBAC-03 viewer: read-only | ✅ Читает (`200`); создать → `403 experiments.create` |
| TC-RBAC-04 защита sole owner | ✅ Снять owner у единственного владельца → `409 Cannot revoke the last owner` |
| TC-RBAC-05 изоляция проектов (IDOR) | ✅ editor к чужому проекту B → `403 experiments.view` / `403 project.members.view` |
| Выдача ролей через UI/прокси | ❌ → [BUG-F-015](https://linear.app/lostpointer/issue/LOS-5/bug-f-015-rbac-project-roles-return-404-apiv1projectsroles-not-routed): `/api/v1/projects/*/roles` = 404 |

**Итого:** RBAC-**enforcement** работает корректно по всем проверенным осям. Но **управление** ролями через auth-proxy сломано (404).

### Найденные баги

| ID | Severity | Кратко |
|----|----------|--------|
| [BUG-F-015](https://linear.app/lostpointer/issue/LOS-5/bug-f-015-rbac-project-roles-return-404-apiv1projectsroles-not-routed) | HIGH | `/api/v1/projects/*/roles` не маршрутизируется на auth-service → 404 (RBAC role-management UI не работает). Вторично: assign-role 500 при payload `{"role"}` вместо `{"role_id"}`. |

---

## 2026-06-10 — conversion profile + backfill (TC-TELE-02/05), UI

| | |
|---|---|
| **Тестировщик** | Claude Code (агент) |
| **Инструмент** | Playwright MCP (UI) + curl (ingest) + проверка в БД |
| **Ветка** | `fix/backend-idempotency-and-auth-hardening` |
| **Окружение** | Локальный dev-стек; датчик `temp_raw` (`df957fee…`) с 3 историческими записями (`physical_value=null`) |
| **Объём** | `telemetry-e2e.md` TC-TELE-02, TC-TELE-05 |

### Результаты

| Сценарий | Итог |
|----------|------|
| TC-TELE-02 Conversion profile draft→active | ✅ Профиль `v1.0` (`physical=2·raw+10`) создан и опубликован; `active_profile_id` проставлен; новый ingest raw=5 → physical=20 |
| TC-TELE-05 Backfill исторических данных | ✅ Задача «Завершён 3/3»; в БД 1.23→12.46, 2.5→15, 3.14→16.28; `null` physical = 0; повторный запуск 0/0 (идемпотентно) |

**Итого:** конверсия raw→physical и backfill работают корректно и end-to-end через UI. Конверсия применяется и на новых ingest, и при пересчёте старых записей.

### Замечания (UX)
- Форма «Создать профиль» не закрывается после успешного сабмита (профиль создаётся, дублей нет, но модалку надо закрывать вручную «×»).
- Во время долгой сессии access-токен периодически истекает; запросы (`backfill`, `error-log`) отдавали `401` без авто-refresh — приходилось перелогиниваться. Кандидат проверить поведение silent-refresh при истёкшем access-токене.

---

## 2026-06-10 — telemetry e2e (TC-TELE), UI + ingest

| | |
|---|---|
| **Тестировщик** | Claude Code (агент) |
| **Инструмент** | Playwright MCP (UI) + curl (ingest sensor-токеном) + проверка в БД |
| **Ветка** | `fix/backend-idempotency-and-auth-hardening` |
| **Окружение** | Локальный dev-стек, Portal `http://localhost:3000` |
| **Setup** | Датчик `temp_raw` (`df957fee…`) в проекте `QA Idempotency Run` |
| **Объём** | `telemetry-e2e.md` TC-TELE-01, 03, 04, 06 |

### Результаты

| Сценарий | Итог |
|----------|------|
| TC-TELE-01 Регистрация датчика + токен | ✅ Создан через UI, токен показан и держится, heartbeat пуст |
| TC-TELE-03 Ingest телеметрии | ✅ 3 reading'а → `202 accepted:3`; в БД 3 записи. `physical_value=null` (профиля нет — TC-TELE-02 не делался) |
| TC-TELE-04 Проверка в UI и БД | ✅ Heartbeat «1 мин назад»; Live SSE подтянул все 3 события; график по `raw` ок; БД совпадает |
| TC-TELE-06 error-log датчика | ❌→✅ Падал `500`; нашёл [BUG-B-005](https://linear.app/lostpointer/issue/LOS-9/bug-b-005-sensor-error-log-returns-500-telemetry-ingest-migrations-not); после миграции `200` |

**Итого:** сквозной путь датчик → токен → ingest → БД → Live SSE в UI работает. По дороге найден баг с миграцией error-log.

### Найденные баги

| ID | Severity | Кратко |
|----|----------|--------|
| [BUG-B-005](https://linear.app/lostpointer/issue/LOS-9/bug-b-005-sensor-error-log-returns-500-telemetry-ingest-migrations-not) | MEDIUM | `sensor_error_log` не создаётся «из коробки» — в docker-compose нет авто-миграции telemetry-ingest → error-log endpoint 500. |

### Замечания
- `physical_value` остаётся `null` без активного conversion profile (ожидаемо). Для проверки конверсии нужно отдельно прогнать TC-TELE-02 + TC-TELE-05 (backfill).
- При выбранном `physical` и отсутствии профиля график Live SSE пуст, хотя raw-данные есть — мелкий UX-нюанс.

---

## 2026-06-10 — CRUD lifecycle (TC-CRUD), UI

| | |
|---|---|
| **Тестировщик** | Claude Code (агент) |
| **Инструмент** | Playwright MCP (headless Chrome 149), проверка в БД |
| **Ветка** | `fix/backend-idempotency-and-auth-hardening` |
| **Окружение** | Локальный dev-стек, Portal `http://localhost:3000`, логин `admin`/`Admin123` |
| **Объём** | `crud-lifecycle.md` TC-CRUD-01, 04, 05 |

### Результаты

| Сценарий | Итог |
|----------|------|
| TC-CRUD-01 Проект и консистентность API↔UI | ✅ Проект из API авто-выбран; эксперименты `idem-exp-1`/`idem-race` видны в UI |
| TC-CRUD-04 Run: создание и переходы | ✅ create→`draft`, «Запустить»→`running`, «Завершить»→`succeeded`; в БД `succeeded` |
| TC-CRUD-05 Capture session: start/stop | ✅ старт→`running`, стоп+confirm→`succeeded` (13с); в БД совпадает |
| TC-CRUD-02 Эксперимент: статусы | ⚙️ Скорректирован: нет кнопки запуска на уровне эксперимента, статусы ведутся через run |

**Итого:** сквозной путь project → experiment → run → capture session работает, UI↔БД консистентны.

### Побочные наблюдения (UX)
- Старт capture session использует нативный `window.prompt` (заметка), стоп — `window.confirm`. Нативные браузерные диалоги — кандидат на замену модалкой (плохая стилизация, ломаются при подавлении диалогов). Не баг, но UX-долг.

---

## 2026-06-10 — idempotency (TC-IDEM)

| | |
|---|---|
| **Тестировщик** | Claude Code (агент) |
| **Инструмент** | curl через auth-proxy (`:8080`, cookie + `X-CSRF-Token` + `Origin`), проверка в БД |
| **Ветка** | `fix/backend-idempotency-and-auth-hardening` |
| **Окружение** | Локальный dev-стек |
| **Setup** | Создан проект `QA Idempotency Run` (`c809124c…`) |
| **Объём** | `idempotency-auth-hardening.md` TC-IDEM-01…03 |

### Результаты

| Сценарий | Итог |
|----------|------|
| TC-IDEM-01 Повтор с тем же ключом+телом | ✅ Оба ответа `201` с одним `id` (replay из кэша), в БД 1 объект |
| TC-IDEM-02 Тот же ключ, другое тело | ✅ `HTTP 409 Conflict` |
| TC-IDEM-03 Конкурентные запросы с одним ключом | ❌ Победитель `201`, проигравший **`500`** (а не replay/409). В БД 1 объект (целостность ок). См. [BUG-B-004](https://linear.app/lostpointer/issue/LOS-8/bug-b-004-concurrent-post-with-same-idempotency-key-http-500) |

**Итого:** базовая идемпотентность (replay, конфликт по телу) работает. Под гонкой — необработанный `UniqueViolationError` → 500.

### Найденные баги

| ID | Severity | Кратко |
|----|----------|--------|
| [BUG-B-004](https://linear.app/lostpointer/issue/LOS-8/bug-b-004-concurrent-post-with-same-idempotency-key-http-500) | MEDIUM | Конкурентные POST с одним `Idempotency-Key` → 500 у проигравшего. Ключ застолбляется после create, а не до. |

### Побочные наблюдения
- В логах experiment-service на каждом create эксперимента: `Audit write failed status=404 action='experiment.create'` — попытка записи в аудит получает 404. Не фейлит запрос (warning), но стоит проверить роутинг audit-эндпоинта.

---

## 2026-06-10 — auth-флоу + smoke-навигация

| | |
|---|---|
| **Тестировщик** | Claude Code (агент) |
| **Инструмент** | Playwright MCP, headless Chrome 149 (`npx playwright install chrome`) |
| **Ветка** | `fix/backend-idempotency-and-auth-hardening` |
| **Окружение** | Локальный dev-стек (`docker compose`), Portal `http://localhost:3000`, Vite dev-сервер |
| **Учётка** | `admin` / `Admin123` (dev-админ) |
| **Объём** | `auth-flow.md` (TC-AUTH-01…06), `smoke-navigation.md` (TC-NAV-01…03) |

### Результаты

| Сценарий | Итог |
|----------|------|
| TC-AUTH-01 Невалидный логин | ✅ Остаёмся на `/login`, тост «Invalid credentials» |
| TC-AUTH-02 Валидный логин | ✅ Редирект на `/experiments`, роль `superadmin` |
| TC-AUTH-03 Логаут | ✅ Редирект на `/login` |
| TC-AUTH-04 Защита роутов | ✅ Заход на `/experiments` без сессии → `/login` |
| TC-AUTH-05 Страница регистрации | ✅ Форма со всеми полями |
| TC-AUTH-06 Страница сброса пароля | ✅ Форма отрисована |
| TC-NAV-01 Прямой заход по URL | ⚠️ Все роуты `200`, кроме `/projects` → **500** |
| TC-NAV-02 Навигация по меню | ✅ Все разделы (включая `/projects`) открываются client-side |
| TC-NAV-03 Чистота консоли | ⚠️ `400` на `/webhooks`; `500` на прямом `/projects` |

**Итого:** auth-флоу — без замечаний. Smoke — 2 дефекта.

### Найденные баги

| ID | Severity | Кратко |
|----|----------|--------|
| [BUG-F-013](https://linear.app/lostpointer/issue/LOS-6/bug-f-013-projects-page-returns-http-500-on-direct-load-f5-reload) | HIGH | `/projects` → HTTP 500 при прямом заходе/перезагрузке. Корень: «голый» префикс `/projects` в API-клиенте + перехват SPA-роута прокси-правилом `vite.config.ts`. |
| [BUG-F-014](https://linear.app/lostpointer/issue/LOS-7/bug-f-014-webhooks-page-sends-requests-without-project-id-400-bad) | MEDIUM | Страница Webhooks шлёт `GET /api/v1/webhooks(/deliveries)` без `project_id` → `400`. Воспроизводится на пустом воркспейсе. |

### Замечания / не покрыто

- Прогон на пустом воркспейсе (0 проектов) — часть состояний (Webhooks с выбранным проектом, списки экспериментов с данными) не проверялась.
- Не покрыты: полный цикл регистрации, сброс пароля по email-ссылке, смена `password_change_required`-пароля, ротация refresh-токена (см. «Не покрыто» в `auth-flow.md`).
- E2E happy-path (`e2e-happy-path.md`) в этот прогон не входил.

---

## Шаблон отчёта

```markdown
## YYYY-MM-DD — <краткое название прогона>

| | |
|---|---|
| **Тестировщик** | |
| **Инструмент** | браузер вручную / Playwright MCP / … |
| **Ветка** | |
| **Окружение** | local dev / staging / … |
| **Учётка** | |
| **Объём** | какие файлы/TC прогонялись |

### Результаты
| Сценарий | Итог |
|----------|------|
| TC-XXX-NN … | ✅ / ⚠️ / ❌ + комментарий |

### Найденные баги
| ID | Severity | Кратко |
|----|----------|--------|

### Замечания / не покрыто
- …
```
