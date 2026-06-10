# Баги — единый список

Все известные баги проекта. Здесь же заводить новые.

---

## Frontend (Experiment Portal)

### BUG-F-001 — Навигация через меню не работает на мобильном браузере
**Приоритет:** HIGH
**Статус:** [x] Исправлен

При тапе на пункт меню с мобильного браузера страница не меняется.

**Возможные причины:** обработчики click/touch, поведение React Router на мобильных браузерах.

---

### BUG-F-005 — Панель телеметрии падает с "Failed to construct 'URL': Invalid URL"
**Приоритет:** HIGH
**Статус:** [x] Исправлен

При открытии панели телеметрии возникает ошибка:
```
Failed to construct 'URL': Invalid URL
```

URL для SSE/WebSocket-эндпоинта собирается из невалидного или пустого значения (скорее всего `undefined` или пустая строка подставляется вместо хоста/пути).

**Возможные причины:**
- Переменная окружения `VITE_API_URL` / `VITE_WS_URL` не задана или пустая
- `sensor_id`, `experiment_id` или другой параметр пути ещё не загружен в момент построения URL
- Неверный базовый URL в конфиге axios/fetch для данного окружения

---

### BUG-F-004 — Live telemetry (SSE) не работает при открытии через информацию о датчике
**Приоритет:** HIGH
**Статус:** [x] Исправлен (та же причина что BUG-F-005)
**Файл:** `projects/frontend/apps/experiment-portal/src/components/SensorDetailModal.tsx` (предположительно)

При открытии Live telemetry через страницу/модал с информацией о датчике SSE-стрим не запускается или не отображает данные.

**Возможные причины:**
- Неверный `sensor_id` или `capture_session_id` передаётся в SSE-эндпоинт
- SSE-соединение открывается до того, как компонент получил нужные данные
- Проблема с авторизацией SSE-запроса (токен не передаётся)

---

### BUG-F-006 — Popup «Создать проект» закрывается при отпускании ЛКМ вне popup
**Приоритет:** MEDIUM
**Статус:** [x] Исправлен
**Файл:** `projects/frontend/apps/experiment-portal/src/components/Modal.tsx`

При отпускании левой кнопки мыши вне области модального окна «Создать проект» окно закрывалось (drag-click: mousedown внутри, mouseup вне — триггерил `onClick` на overlay).

**Исправление:** добавлен `useRef<boolean>` (`mouseDownOnOverlay`) и `onMouseDown` на overlay. `onClose` вызывается только если и `mousedown`, и `mouseup` (click) произошли на самом overlay (`e.target === e.currentTarget`). Также исправлено нарушение правил хуков — `useEffect` вынесен до условного `return null`.

---

### BUG-F-007 — Токен датчика показывается слишком коротко после создания (~2 с)
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Файл:** `projects/frontend/apps/experiment-portal/src/pages/CreateSensor.tsx`

После создания датчика токен отображался около 2 секунд — не успеваешь прочитать и скопировать.

**Исправление:** убран `setTimeout` с авто-редиректом в `onSuccess` (страница с токеном оставалась видимой только 3000 мс). Текст подсказки переформулирован: пользователь сам нажимает «Перейти к датчику» после копирования. Добавлен регрессионный тест, проверяющий, что `mockNavigate` не вызывается автоматически и токен остаётся видимым спустя >3 с.

---

### BUG-F-008 — После тестовой отправки сообщения датчику — logout и редирект на страницу входа
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Файл:** `projects/frontend/apps/experiment-portal/src/api/client.ts`

После тестовой отправки сообщения датчику происходил выход из системы и редирект на страницу входа.

**Корневая причина:** `telemetryApi.ingest()` использует `apiClient.post(...)` с sensor-токеном в `Authorization`, но глобальный response-interceptor `apiClient` на любой 401 считал, что протухла user-сессия: пытался `POST /auth/refresh`, а на ошибке рефреша делал `window.location.href = '/login'`. Если sensor-токен неверный — 401 про **sensor**, refresh user-сессии тут бесполезен, а редирект выкидывал пользователя из приложения.

**Исправление:** в interceptor добавлена проверка флага `_skipAuthInterceptor`; `telemetryApi.ingest` передаёт его в config. Флаг делает интент явным и переиспользуемым для других будущих запросов с нестандартной авторизацией. Добавлен unit-тест в `client.test.ts`, проверяющий что при установленном флаге 401 не вызывает refresh и не меняет `window.location.href`.

---

### BUG-F-009 — Вкладка «Системные логи» выдаёт 404
**Приоритет:** HIGH
**Статус:** [x] Исправлен (та же причина что BUG-F-012)

При переходе во вкладку «Системные логи» страница отвечает 404.

**Корневая причина:** `GET /api/v1/audit-log` проксировался в experiment-service через generic `/api` прокси, но эндпоинт реализован в **auth-service**. Аналогично BUG-B-003.

**Исправление:** в auth-proxy добавлен явный маршрут `/api/v1/audit-log` → auth-service (до generic `/api` прокси). Также `audit.ts` переведён на `apiClient` (см. BUG-F-012).

---

### BUG-F-010 — При создании системной роли: «CSRF token missing or invalid»
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Файлы:** `projects/frontend/apps/experiment-portal/src/api/permissions.ts`, `projects/frontend/apps/auth-proxy/src/index.ts`

При попытке создать системную роль выдавалась ошибка:
```
Ошибка
×
CSRF token missing or invalid
```

**Корневые причины:**
1. `permissions.ts` создавал отдельный axios-инстанс без CSRF-интерцептора → `X-CSRF-Token` не добавлялся в заголовки state-changing запросов
2. `POST /api/v1/system-roles` уходил в experiment-service (generic `/api` прокси), хотя эндпоинт живёт в **auth-service**

**Исправление:** `permissions.ts` переведён на `apiGet/Post/Patch/Delete` из `client.ts` (у которого есть CSRF-интерцептор). В auth-proxy добавлен явный маршрут `/api/v1/system-roles` → auth-service.

---

### BUG-F-011 — В таблице инвайтов использованный инвайт показывает прочерк вместо даты использования
**Приоритет:** MEDIUM
**Статус:** [x] Исправлен
**Файл:** `projects/backend/services/auth-service/src/auth_service/services/auth.py`

Неактивный инвайт, по которому зарегистрировались, в столбце «Использован» показывал прочерк (`—`) вместо даты.

**Корневая причина:** при `registration_mode == "open"` (дефолт) функция `register()` никогда не вызывала `mark_used` — `used_at` оставался `null` в БД. Инвайт становился неактивным только по истечении срока действия, но не по факту использования.

**Исправление:** валидация и вызов `mark_used` вынесены из блока `if registration_mode == "invite"` — теперь если инвайт-токен передан при регистрации в любом режиме, он проверяется и помечается использованным.

---

### BUG-F-012 — При переходе в Аудит-лог выдаётся 404
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Файлы:** `projects/frontend/apps/experiment-portal/src/api/audit.ts`, `projects/frontend/apps/auth-proxy/src/index.ts`

При попытке открыть Аудит-лог выдавалась ошибка:
```
Ошибка
×
Request failed with status code 404
```

**Корневая причина:** `GET /api/v1/audit-log` проксировался в experiment-service через generic `/api` прокси, но эндпоинт реализован в **auth-service**. Аналогично BUG-B-003.

**Исправление:** в auth-proxy добавлен явный маршрут `/api/v1/audit-log` → auth-service (до generic `/api` прокси). `audit.ts` переведён на `apiGet` из `client.ts`, убран дублирующий axios-инстанс.

---

### BUG-F-015 — Управление проектными ролями (RBAC) возвращает 404: `/api/v1/projects/*/roles` не маршрутизируется на auth-service
**Приоритет:** HIGH
**Статус:** [ ] Не исправлен
**Файлы:** `projects/frontend/apps/auth-proxy/src/index.ts` (нет явного маршрута), `projects/frontend/apps/experiment-portal/src/api/permissions.ts`

Любые операции управления проектными ролями через auth-proxy возвращают **404 Not Found**:
- `GET /api/v1/projects/{id}/roles` (список ролей проекта)
- `POST/PATCH/DELETE /api/v1/projects/{id}/roles[/{role_id}]`
- `POST/DELETE /api/v1/projects/{id}/members/{user_id}/roles[/{role_id}]` (выдача/отзыв роли участнику)

Фронт (`permissions.ts`) дёргает именно эти пути → раздел управления доступом проекта не работает. Найдено через RBAC-прогон (2026-06-10).

**Корневая причина:** эти эндпоинты реализованы в **auth-service** (`api/routes/project_roles.py`), но в auth-proxy нет явного маршрута для `/api/v1/projects/*` — generic `/api`-прокси (`prefix: '/api'`, `upstream: targetExperimentUrl`) отправляет всё в **experiment-service**, где таких роутов нет → 404. В auth-proxy явно проброшены только `/api/v1/users`, `/api/v1/system-roles`, `/api/v1/permissions`, `/api/v1/audit-log` — `/api/v1/projects/*/roles` забыли. Тот же класс, что [BUG-B-003](#bug-b-003)/BUG-F-009/F-010/F-012.

**Подтверждение:** `GET /api/v1/projects/{id}/roles` через прокси → 404; прямой запрос в auth-service `:8001` → 401 (роут существует). Выдача ролей для прогона RBAC выполнена в обход прокси (прямо в auth-service) — enforcement при этом работает (viewer read-only, editor не управляет ролями, защита sole-owner, IDOR между проектами — всё ✅).

**Связанная вторичная проблема:** хендлер назначения роли (`POST .../members/{user_id}/roles`) при передаче `{"role":"editor"}` (имя роли) падает с **HTTP 500** `{"error":"'role_id'"}` (KeyError), хотя `dto.py` декларирует приём и `role_id`, и `role`. Корректно отрабатывает только `{"role_id":"<uuid>"}`. Должен принимать имя роли или возвращать 400, а не 500.

**Исправление (кандидат):** добавить в auth-proxy явный маршрут `/api/v1/projects` → auth-service ПЕРЕД generic `/api`-прокси (по аналогии с `/api/v1/system-roles`). Отдельно — починить приём `role` по имени в хендлере assign-role (или явный 400). Перепроверить весь класс «голый /api → experiment-service» (см. заметку про схему маршрутизации auth-proxy).

---

### BUG-F-003 — Не работает кнопка копирования токена датчика
**Приоритет:** HIGH
**Статус:** [x] Исправлен

Кнопка копирования токена датчика не работает. Вероятно та же причина, что и BUG-F-002 — `navigator.clipboard` недоступен вне HTTPS.

---

### BUG-F-002 — `navigator.clipboard.writeText` падает с TypeError
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Файл:** `projects/frontend/apps/experiment-portal/src/pages/AdminUsers.tsx:112,120,269`

```
Uncaught TypeError: Cannot read properties of undefined (reading 'writeText')
    at handleCopyInviteLink / handleCopyToken / onClick
```

`navigator.clipboard` доступен только в **безопасном контексте** (HTTPS или localhost). При работе через HTTP (например, по локальной сети или staging без TLS) объект `undefined`.

Затронуто три места:
- `handleCopyToken` — копирование токена приглашения
- `handleCopyInviteLink` — копирование ссылки приглашения
- `onClick` на сброшенном пароле

**Исправление:** добавить фолбэк через `document.execCommand('copy')` или показывать ошибку если буфер обмена недоступен:
```ts
if (navigator.clipboard) {
    await navigator.clipboard.writeText(text)
} else {
    // fallback или toast с текстом для ручного копирования
}
```

---

### BUG-F-013 — Страница «Проекты» (`/projects`) падает с HTTP 500 при прямом заходе / перезагрузке
**Приоритет:** HIGH
**Статус:** [ ] Не исправлен
**Файлы:** `projects/frontend/apps/experiment-portal/vite.config.ts`, `projects/frontend/apps/experiment-portal/src/api/projects.ts`

Прямой переход по ссылке или перезагрузка (F5) на `http://localhost:3000/projects` отдаёт **HTTP 500** (`Content-Type: text/plain`). Client-side навигация на ту же страницу (клик по пункту меню «Проекты») работает — поэтому при обычной работе баг не виден, но любой reload на этой странице её ломает. Найдено через Playwright smoke-тест (2026-06-10). Затронут только этот роут — остальные (`/experiments`, `/sensors`, `/telemetry`, `/admin/*`) отдают 200.

**Корневая причина:** `src/api/projects.ts` ходит в API по «голому» префиксу `/projects` (без `/api/v1/`), в отличие от всех остальных API-клиентов (`/api/v1/...`). Из-за этого в `vite.config.ts` стоит прокси-правило `'/projects' → authProxyUrl`, которое перехватывает и одноимённый SPA-роут страницы «Проекты»: Vite проксирует запрос на бэкенд вместо отдачи `index.html` → 500. Это тот же класс проблем, что BUG-B-003 (коллизия схемы маршрутизации).

**Исправление:** перевести projects API на консистентный префикс `/api/v1/projects` и убрать спец-правило `'/projects'` из `vite.config.ts`. Проверить, что та же коллизия не воспроизводится в prod nginx-конфиге.

---

### BUG-F-014 — Страница Webhooks шлёт запросы без `project_id` → 400 Bad Request
**Приоритет:** MEDIUM
**Статус:** [ ] Не исправлен
**Файл:** `projects/frontend/apps/experiment-portal` (страница Webhooks / её API-клиент)

При открытии `/webhooks` фронт делает `GET /api/v1/webhooks?page_size=100` и `GET /api/v1/webhooks/deliveries?page=1&page_size=20` без параметра `project_id`. Бэкенд требует его и отвечает `400: project_id is required. Provide it in query parameter, request body, or X-Project-Id header`. Воспроизводится на пустом воркспейсе (нет проектов → нечего подставить). Найдено через Playwright smoke-тест (2026-06-10).

**Возможная причина:** запрос летит на маунте компонента без guard'а на выбранный проект.

**Исправление:** не отправлять запросы webhooks, пока проект не выбран (показывать состояние «выберите проект»), либо корректно прокидывать `project_id`/`X-Project-Id`.

---

## Backend (Experiment Service)

### BUG-B-001 — Worker `activate_scheduled_profiles` падает с `invalid input value for enum conversion_profile_status: "archived"`
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Файл:** `projects/backend/services/experiment-service/src/experiment_service/workers/activate_scheduled_profiles.py:30`

Воркер падал при каждом запуске с ошибкой:
```
asyncpg.exceptions.InvalidTextRepresentationError:
invalid input value for enum conversion_profile_status: "archived"
```

В коде воркера использовалось значение `"archived"` для enum `conversion_profile_status`, которого не существует в БД. Допустимые значения: `draft`, `scheduled`, `active`, `deprecated`.

**Исправление:** заменён `"archived"` на `"deprecated"` в SQL-запросе. Дополнительно: при авто-активации `draft → active` теперь обновляется `sensor.active_profile_id` (ранее не обновлялось — датчик не получал активный профиль и конверсия не применялась).

---

### BUG-B-002 — В Live telemetry (SSE) не пересчитываются значения (raw → physical)
**Приоритет:** HIGH
**Статус:** [x] Исправлен (корневая причина устранена в BUG-B-001)

В `TelemetryStreamModal` данные приходили, но `physical_value` отсутствовал или равнялся `null`.

**Анализ:** SSE-эндпоинт читает `physical_value` из `telemetry_records` как есть — конверсия происходит в момент ingesta в `TelemetryIngestService._prepare_items`. Тот использует `ProfileCache` (TTL 60 с), который читает активный профиль через `sensor.active_profile_id`. Воркер `activate_scheduled_profiles` падал (BUG-B-001), из-за чего `draft`-профили не активировались и `sensor.active_profile_id` не обновлялся → кэш возвращал `None` → `physical_value = null`.

**Для исторических данных** с `physical_value = null` необходимо запустить backfill:
`POST /api/v1/sensors/{sensor_id}/conversion-profiles/{profile_id}/backfill`

---

### BUG-B-003 — `GET /api/v1/sensors/{sensor_id}/error-log` возвращает 404
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Файл:** `projects/frontend/apps/auth-proxy/src/index.ts`

Запрос с фронта:
```
GET /api/v1/sensors/1a9d0362-815e-4683-94b1-2767dfa501f5/error-log
  ?limit=25&offset=0&project_id=3739a924-37e1-402c-b9c7-fbf27f118ded
```
Отвечал: `404 Not Found`.

**Корневая причина:** эндпоинт реализован в **telemetry-ingest-service** (таблица `sensor_error_log`, REST/WS обработчики ingest пишут в неё). Но auth-proxy маршрутизировал все `/api/v1/sensors/*` в experiment-service через generic `/api`-префикс — там обработчика нет, отсюда 404.

**Исправление:** в auth-proxy перед регистрацией `/api`-прокси добавлен явный маршрут `GET /api/v1/sensors/:sensorId/error-log`, пробрасывающий запрос в `targetTelemetryUrl` с сохранением trace/request-id и access-токена из cookie. Добавлен тест, проверяющий что запрос уходит в telemetry-ingest, а не в experiment-service.

---

### BUG-B-004 — Конкурентные POST с одним `Idempotency-Key` → HTTP 500 (UniqueViolationError протекает)
**Приоритет:** MEDIUM
**Статус:** [ ] Не исправлен
**Файлы:** `projects/backend/services/experiment-service/src/experiment_service/api/routes/experiments.py:123-157`, `services/idempotency.py`

Два одновременных идентичных `POST /api/v1/experiments` с одним `Idempotency-Key` (и одинаковым телом): один запрос получает `201`, второй — **HTTP 500** (`{"error":"Internal server error"}`). Найдено через ручной прогон TC-IDEM-03 (2026-06-10), воспроизводится стабильно.

**Корневая причина:** idempotency-ключ «застолбляется» через `store_response()` **после** `service.create_experiment()`, а не до бизнес-операции. При гонке оба запроса проходят `get_cached_response()` (записи в кэше ещё нет) и оба вызывают `create`. Победитель создаёт эксперимент, проигравший упирается в БД-constraint `experiments_project_name_uindex` → `asyncpg.exceptions.UniqueViolationError`, которое не перехватывается в `create_experiment` (ловятся только `IdempotencyConflictError` и `InvalidStatusTransitionError`) → необработанное исключение → 500.

Целостность данных при этом **сохраняется** (в БД ровно 1 объект) — но исключительно благодаря побочному unique-constraint на `(project_id, lower(name))`, а не самому idempotency-механизму. Для эндпоинтов без такого «страхующего» уникального индекса гонка могла бы привести к дублям.

**Ожидаемое поведение:** проигравший конкурентный запрос должен получить либо реплей ответа победителя (`201` с тем же телом), либо чистый `409` — но не `500`.

**Исправление (кандидат):** застолбить ключ ДО бизнес-операции (вставка idempotency-записи в состоянии `in_progress` с уникальным индексом по ключу, конкурент получает 409/ждёт результат), либо как минимум перехватывать `UniqueViolationError` в `create_experiment` и конвертировать в replay/409. Распространить на runs/sensors/capture-sessions (тот же паттерн). Связано с `feb640e`.

---

### BUG-B-005 — `GET /api/v1/sensors/{id}/error-log` → 500: таблица `sensor_error_log` не создаётся (нет авто-миграции telemetry-ingest)
**Приоритет:** MEDIUM
**Статус:** [ ] Не исправлен
**Файлы:** `docker-compose.yml` (нет сервиса telemetry-migrate), `projects/backend/services/telemetry-ingest-service/migrations/005_sensor_error_log.sql`

На странице датчика (`/sensors/{id}`) всплывает тост «Ошибка запроса», в консоли — `GET /api/v1/sensors/{id}/error-log → 500`. Найдено через Playwright-прогон telemetry e2e (2026-06-10).

**Корневая причина:** в логах telemetry-ingest — `asyncpg.exceptions.UndefinedTableError: relation "sensor_error_log" does not exist`. Таблица создаётся миграцией `005_sensor_error_log.sql`, но в `docker-compose.yml` есть авто-сервисы только `auth-migrate` и `experiment-migrate` — **сервиса для миграций telemetry-ingest нет**. Миграции telemetry-ingest применяются лишь вручную через `make telemetry-ingest-migrate`. На чистом `make dev-up` миграция 005 не применяется → в `experiment_db` есть только 001-004 (experiment-service), а `sensor_error_log` отсутствует → эндпоинт 500 «из коробки».

Это развитие [BUG-B-003](#bug-b-003): после фикса роутинга запрос доходит до telemetry-ingest, но падает уже на уровне БД. Риск распространяется на прод/CI, если деплой-пайплайн тоже не применяет миграции telemetry-ingest.

**Подтверждение:** после `make telemetry-ingest-migrate` таблица создаётся, эндпоинт возвращает `200 {"entries":[],"total":0,...}`.

**Исправление (кандидат):** добавить в `docker-compose.yml` init-сервис `telemetry-migrate` (по аналогии с `experiment-migrate`), от которого зависит `telemetry-ingest-service`. Проверить деплой-пайплайн (Terraform/CI) на применение миграций telemetry-ingest.

---

## Observability / Grafana

### BUG-O-001 — В Grafana (прод) не работают логи, нельзя выбрать лейблы
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Среда:** production

В Grafana Explore / Logs нет доступных лейблов — источник логов (Loki) либо не подключён, либо не передаёт данные. Логи не отображаются.

**Возможные причины:**
- Alloy не запущен или не достигает Loki в проде (неверный адрес, firewall)
- Datasource Loki в Grafana не настроен для prod-окружения
- Loki не получает данные от сервисов (неверный endpoint в конфиге Alloy/sidecar)
- Loki запущен, но retention/ingestion pipeline сломан

**Что проверить:**
1. Статус контейнеров Loki и Alloy в проде
2. Настройки datasource в Grafana (`/datasources`)
3. Логи самого Alloy на наличие ошибок отправки
4. Сетевую доступность порта 3100 (Loki) из Alloy

---

## Firmware (RC Vehicle)

### BUG-RC-003 — Failsafe не потокобезопасен
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Файл:** `projects/rc_vehicle/firmware/common/failsafe.cpp`

`last_active_ms_`, `state_`, `initialized_` читаются и пишутся из разных FreeRTOS-задач без синхронизации.

**Исправление:** добавить `mutable std::mutex mutex_`, взять lock в `Update()`, `IsTriggered()`, `Reset()`.

---

### BUG-RC-004 — Нестабильный угол заноса при нулевой скорости
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Файл:** `projects/rc_vehicle/firmware/common/vehicle_ekf.cpp:140-142`

```cpp
return std::atan2(x_[1], x_[0]);  // нестабильно при vx≈0, vy≈0
```

**Исправление:** вернуть `0.0f` при `GetSpeedMs() < 0.05f`.

---

### BUG-RC-005 — Переполнение uint32_t в расчёте dt_ms
**Приоритет:** MEDIUM
**Статус:** [x] Исправлен
**Файл:** `projects/rc_vehicle/firmware/common/vehicle_control_unified.cpp`

После ~49 дней работы `millis()` переполняется — один цикл получает огромный `dt_ms`.

**Исправление:** `const uint32_t dt_ms = static_cast<uint32_t>(now - last_loop_ms_);`

---

### BUG-RC-006 — NVS Load не вызывает Clamp()
**Приоритет:** MEDIUM
**Статус:** [x] Исправлен
**Файл:** `projects/rc_vehicle/firmware/esp32_common/stabilization_config_nvs.cpp:31-42`

Данные из NVS могут быть из старой прошивки с другим диапазоном значений — `Clamp()` не вызывается.

**Исправление:** добавить `config.Clamp()` после `config.IsValid()`.

---

### BUG-RC-007 — LPF Butterworth: нет guard на граничные частоты
**Приоритет:** MEDIUM
**Статус:** [x] Исправлен
**Файл:** `projects/rc_vehicle/firmware/common/lpf_butterworth.cpp:16-26`

`tan(pi * fc / fs)` при `fc == fs/2` даёт `+inf`.

**Исправление:**
```cpp
if (cutoff_hz <= 0.0f || cutoff_hz >= sample_rate_hz * 0.5f) return false;
```

---

### BUG-RC-008 — RxBuffer::Advance() молча обрезает данные
**Приоритет:** LOW
**Статус:** [x] Исправлен
**Файл:** `projects/rc_vehicle/firmware/common/uart_bridge_base.hpp:74-77`

```cpp
if (pos_ > CAPACITY) pos_ = CAPACITY;  // молчаливая потеря данных
```

**Исправление:** `assert(pos_ + n <= CAPACITY)` в debug-сборке.

---

### BUG-RC-009 — nullptr не проверяется в Init() контроллеров
**Приоритет:** LOW
**Статус:** [x] Исправлен
**Файл:** `projects/rc_vehicle/firmware/common/stabilization_pipeline.cpp:12-18`

`YawRateController::Init()`, `PitchCompensator::Init()` принимают указатели без проверки на `nullptr`.

**Исправление:** `assert(ptr != nullptr)` в `Init()`.

---

### BUG-RC-010 — Произвольный порог в UpdateGyroZ
**Приоритет:** LOW
**Статус:** [x] Исправлен
**Файл:** `projects/rc_vehicle/firmware/common/vehicle_ekf.cpp:101`

```cpp
if (S < 1e-9f) return;  // порог выбран без обоснования
```

**Исправление:** заменить на `if (S < params_.r_gz * 1e-3f)` или задокументировать константу.

---

### BUG-RC-011 — Ускорение при движении назад в режиме торможения
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Файл:** `projects/rc_vehicle/firmware/common/control_loop_processor.cpp:138-140`, `projects/rc_vehicle/firmware/common/kids_mode_processor.cpp:31-35`

При включённом режиме торможения (`braking_mode = Brake`, не накат) при движении назад машина начинает постоянно ускоряться вместо торможения/замедления.

**Ожидаемое поведение:** при движении назад (отрицательная скорость) и отпускании газа/торможении машина должна замедляться.

**Возможная причина:** логика применения `brake_slew_multiplier` или знак throttle не учитывается при движении назад.

**Исправление:** проверить применение `brake_slew_multiplier` в `control_loop_processor.cpp`, логику ограничения `reverse_limit` в `kids_mode_processor.cpp`. Требуется отладка на реальном устройстве с телеметрией.

---

### BUG-RC-012 — Машина не двигается при малых значениях throttle в тестовых режимах
**Приоритет:** MEDIUM
**Статус:** [x] Исправлен
**Файл:** `projects/rc_vehicle/firmware/common/calibration_manager.cpp`, `projects/rc_vehicle/firmware/common/test_runner.cpp`, `projects/rc_vehicle/firmware/common/speed_calibration.cpp`

В автоматических тестовых режимах (авто-калибровка, тестовые прогоны) машина стоит на месте при малых значениях throttle.

**Ожидаемое поведение:** машина должна начинать движение при любых положительных значениях throttle.

**Возможные причины:**
1. **Мёртвая зона ESC:** ESC не реагирует на PWM < ~1550 мкс (требуется калибровка минимального газа)
2. **Неверная калибровка ESC:** не пройдена процедура калибровки мин/макс PWM для конкретного ESC
3. **Слишком низкий `kAccelerateThrottle`:** значение по умолчанию недостаточно для преодоления трения/инерции
4. **Failsafe срабатывает слишком рано:** 250 мс таймаут может срабатывать до начала движения

**Исправление:**
- Добавить калибровку минимального рабочего PWM ESC (процедура: найти мин. PWM при котором мотор начинает вращаться)
- Увеличить `kAccelerateThrottle` в тестовых режимах (например, с 0.2 до 0.3-0.4)
- Проверить/увеличить таймаут failsafe для тестовых режимов
- Добавить проверку фактического движения по IMU (accel > порога) перед продолжением теста

**Зависимости:** требуется отладка на реальном устройстве с телеметрией PWM и IMU.

