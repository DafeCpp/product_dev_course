# Сценарии: телеметрия end-to-end

Полный путь данных: регистрация датчика → выдача токена → ingest через telemetry-ingest-service → конверсия `raw → physical` по conversion profile → отображение в UI (Live SSE) и проверка в БД (TimescaleDB), heartbeat и connection status.

**Где реализовано:**
- Ingest: `POST /api/v1/telemetry` (telemetry-ingest-service, `:8003`), авторизация sensor-токеном `Authorization: Bearer <SENSOR_TOKEN>`.
- Конверсия: `TelemetryIngestService._prepare_items` + `ProfileCache` (TTL 60с), активный профиль через `sensor.active_profile_id`.
- Хранение: `telemetry_records` (TimescaleDB, `experiment_db`).
- Профили: `ConversionProfileStatus` = `draft → scheduled → active → deprecated`; backfill: `POST /api/v1/sensors/{sensor_id}/conversion-profiles/{profile_id}/backfill`.

**Предусловие:** авторизован как `admin`; есть проект, эксперимент, run и capture session (см. [`crud-lifecycle.md`](crud-lifecycle.md)).

---

### TC-TELE-01 — Регистрация датчика и выдача токена
**Предусловие:** активный проект, открыт `/sensors`.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | «Создать сенсор»: name `temp_raw`, type `thermocouple`, input_unit `mV`, display_unit `C` | Сенсор создан |
| 2 | На экране создания скопировать **sensor token** (одноразовая выдача) | Токен виден достаточно долго (см. BUG-F-007), копирование работает (фолбэк по BUG-F-002/003) |
| 3 | Открыть детали сенсора | `last_heartbeat` пустой (до первой телеметрии), status соответствует |

**Факт (2026-06-10):** ✅ Прогнано через Playwright. Датчик `temp_raw` (type `temperature`, mV→C) создан, токен показан и держится на экране (фикс BUG-F-007), копирование доступно. До телеметрии heartbeat пустой.

---

### TC-TELE-02 — Conversion profile: draft → active
**Предусловие:** есть сенсор `temp_raw`.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Создать conversion profile (формула raw→physical, напр. линейная) в статусе `draft` | Профиль создан |
| 2 | Активировать профиль (`draft → active`) | `sensor.active_profile_id` указывает на профиль (см. BUG-B-001: авто-активация обновляет это поле) |

**Факт (2026-06-10):** ✅ Прогнано через Playwright. Создан профиль `v1.0` (линейный `physical = 2·raw + 10`, предпросмотр в форме подтвердил raw=10→30). Статус `draft` → «Опубликовать» (confirm) → `active`. `active_profile_id` проставлен (`86a8f909…`). Новый ingest raw=5 → **physical=20** в БД (конверсия применяется).
> ⚠️ UX-наблюдение: форма «Создать профиль» не закрывается автоматически после успешного сабмита (профиль создаётся, но модалка остаётся открытой — приходится закрывать «×»). Дублей не создаёт.

---

### TC-TELE-03 — Ingest телеметрии и конверсия
**Предусловие:** `SENSOR_ID`, `SENSOR_TOKEN`, `RUN_ID`, `CAPTURE_SESSION_ID`, активный профиль.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | `POST /api/v1/telemetry` с raw-значениями | Ответ `{"status":"accepted","accepted":N}` (`200`/`202`) |
| 2 | Дождаться обработки | Записи в `telemetry_records` с заполненным `physical_value` (не `null` — см. BUG-B-002) |

**Факт (2026-06-10):** ✅ Отправлено 3 reading'а sensor-токеном → `{"status":"accepted","accepted":3}`, `HTTP 202`. В БД 3 записи. ⚠️ `physical_value = null` у всех — **ожидаемо**, т.к. TC-TELE-02 (активный профиль) в этом прогоне не выполнялся (`active_profile_id` пуст).

```bash
curl -fsS -X POST http://localhost:8003/api/v1/telemetry \
  -H "Authorization: Bearer <SENSOR_TOKEN>" -H 'Content-Type: application/json' \
  -d "{\"sensor_id\":\"<SENSOR_ID>\",\"run_id\":\"<RUN_ID>\",
       \"capture_session_id\":\"<CAPTURE_SESSION_ID>\",
       \"readings\":[{\"timestamp\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",\"raw_value\":1.23}]}"
```

---

### TC-TELE-04 — Проверка результата в UI и БД
**Предусловие:** телеметрия отправлена (TC-TELE-03).

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Открыть детали сенсора | `last_heartbeat` обновился (не пустой) |
| 2 | Открыть Live telemetry (SSE) для сенсора/сессии | Значения приходят, `physical_value` рассчитан (см. BUG-F-004/005 — URL SSE корректный) |
| 3 | Проверить БД | `count >= 1` записей |

**Факт (2026-06-10):** ✅ Прогнано через Playwright. Heartbeat в UI → «1 мин назад». Live telemetry (SSE) открылся без ошибки URL (регресс BUG-F-005 не воспроизводится); при `since_ts` раньше ingest пришли все 3 события (`#1 raw=1.23`, `#2 raw=2.5`, `#3 raw=3.14`), `events: 3`. График по `raw` отрисован (last=3.140, min=1.230, max=3.140); по `physical` — пусто (профиля нет).

```bash
docker exec backend-postgres psql -U postgres -d experiment_db -c \
  "select count(*), count(physical_value) from telemetry_records where sensor_id='<SENSOR_ID>';"
```

---

### TC-TELE-05 — Backfill для исторических данных
**Предусловие:** есть записи с `physical_value = null` (ingest до активации профиля).

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | «Запустить пересчёт» (`POST /api/v1/sensors/{sensor_id}/backfill`) → confirm | Задача backfill запущена |
| 2 | Проверить БД после завершения | `physical_value` заполнен для исторических записей |
| 3 | Backfill из чужого проекта (негативный) | Запрещено (project-scoped, IDOR-фикс `66085f5`) |

**Факт (2026-06-10):** ✅ Прогнано через Playwright. После активации профиля кнопка «Запустить пересчёт» разблокировалась. Запуск → задача «Завершён 3/3»; повторный запуск → «Завершён 0/0» (идемпотентно). В БД все исторические записи пересчитаны: 1.23→**12.46**, 2.5→**15**, 3.14→**16.28** (формула `2·raw+10`); записей с `null` physical_value — **0**. Шаг 3 (чужой проект) не прогонялся (нужен 2-й проект/пользователь).

---

### TC-TELE-06 — Heartbeat и connection status (монитор)
**Предусловие:** датчик отправлял телеметрию.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Открыть `/sensor-monitor` | Датчик отображается; статус подключения и история heartbeat видны |
| 2 | Прекратить отправку, подождать | Connection status переходит в offline/stale по таймауту |
| 3 | Запрос error-log датчика (`GET /api/v1/sensors/{id}/error-log`) | Отдаётся из telemetry-ingest (см. BUG-B-003), не 404 |

**Факт (2026-06-10):** ❌→✅ На странице датчика error-log падал с **500** (`relation "sensor_error_log" does not exist`) — нашёл [BUG-B-005](https://linear.app/lostpointer/issue/LOS-9/bug-b-005-sensor-error-log-returns-500-telemetry-ingest-migrations-not): миграция telemetry-ingest 005 не применяется авто-сервисом в docker-compose. После `make telemetry-ingest-migrate` эндпоинт вернул `200 {"entries":[],"total":0}`, тост в UI исчез.

---

### TC-TELE-07 — Негативные: невалидный токен / чужой сенсор
| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Ingest с неверным sensor-токеном | `401`; **не** logout пользователя в UI (см. BUG-F-008 — `_skipAuthInterceptor`) |
| 2 | Ingest с `sensor_id`, не принадлежащим токену | Отклонено (`403`/`401`), запись не создаётся |
| 3 | Ingest без `capture_session_id`/в завершённую сессию | Поведение по спецификации, без `500` |

---

## Не покрыто / на доработку
- WebSocket-ingest (помимо REST) и disk spool при недоступности БД.
- Бинарный download телеметрии (планируется, см. заметку проекта).
- Нагрузочные сценарии (частота 500 Гц с RC-машинки).

> Статус: сценарии **описаны, ещё не прогонялись** вручную. Результаты — в [`test-reports.md`](test-reports.md).
