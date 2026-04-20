# RFC-0001: config-service

**Статус:** Review

**Автор(ы):** Ivan Khokhlov

**Дата:** 17-04-2026

**Тип:** New Service

---

## Резюме

Сервис для управления runtime-конфигурацией системы: поднять таймауты походов в другие сервисы или БД, включить/выключить feature flag, перевести функциональность в режим деградации при инциденте. Сейчас такие изменения требуют правки конфигов и перезапуска сервисов, что приводит к простою и не даёт оперативно реагировать на проблемы в production.

Основной сценарий — **incident response**: дежурный через админский UI меняет конфигурацию, сервисы-потребители подхватывают изменения за несколько секунд без рестарта.

---

## Мотивация

Почему нужен новый сервис? Какие проблемы в текущей архитектуре он решает?

- **Проблема 1: Отсутствие runtime конфигурации** - Сейчас для изменения параметров поведения системы (таймауты, feature flags, пороги деградации) требуется изменить конфигурационные файлы и перезапустить сервис, что приводит к простою и невозможности оперативного реагирования на проблемы в production.

- **Проблема 2: Децентрализованное управление конфигурацией** - Конфигурация разбросана по разным сервисам в переменных окружения, ConfigMap'ах Kubernetes или локальных файлах, что усложняет аудит, версионирование и обеспечение консистентности.

- **Ограничения текущего подхода** - Нельзя оперативно реагировать на инциденты (например, увеличить таймаут к проблемному внешнему API или отключить проблемную функциональность без кодовых изменений и деплоя).

---

## Дизайн сервиса

### Основные компоненты

| Компонент | Описание | Технология |
|-----------|---------|-----------|
| API | REST эндпоинты (JSON over HTTP) | Python aiohttp |
| Хранилище конфигурации | Таблицы `configs` и `config_history` | PostgreSQL |
| Валидатор | Проверка `value` против JSON-Schema на каждую запись | jsonschema (Python) |

### API

Основные эндпоинты для управления конфигурацией:

```
POST   /api/v1/config                 # Создание конфигурации (поддерживает Idempotency-Key и ?dry_run=true)
GET    /api/v1/config?service=&project=&config_type=&is_active=&limit=50&cursor=  # Список с фильтрами и cursor-pagination
GET    /api/v1/config/{config_id}     # Получение конкретной конфигурации по ID
PATCH  /api/v1/config/{config_id}     # Частичное обновление (change_reason обязателен; поддерживает ?dry_run=true)
DELETE /api/v1/config/{config_id}     # Soft delete (выставляет deleted_at; не возвращается в list/bulk)
POST   /api/v1/config/{config_id}/activate   # Активация конфигурации
POST   /api/v1/config/{config_id}/deactivate # Деактивация конфигурации
POST   /api/v1/config/{config_id}/rollback   # Откат к указанной версии из config_history
GET    /api/v1/config/{config_id}/history    # История изменений конфигурации

GET    /api/v1/schemas                       # Список активных схем по всем config_type
GET    /api/v1/schemas/{config_type}         # Текущая активная схема для типа
GET    /api/v1/schemas/{config_type}/history # История версий схемы
PUT    /api/v1/schemas/{config_type}         # Загрузка новой версии схемы (с compat-check)
```

#### Семантика API

**Пагинация и фильтры (`GET /api/v1/config`):**
- Фильтры: `service`, `project`, `config_type`, `is_active` — все optional, AND-композиция.
- Cursor-based pagination: параметр `limit` (по умолчанию 50, максимум 500) и `cursor` (opaque-строка из предыдущего ответа).
- Ответ: `{ "items": [...], "next_cursor": "..." | null }`.
- Offset-pagination не используется — cursor лучше ведёт себя на больших наборах и при параллельных изменениях.

**Семантика `PATCH`:**
- Top-level поля (`description`, `metadata`, `is_active`) мержатся: поле, не переданное в запросе, остаётся прежним.
- Поле `value` **всегда заменяется целиком** — JSONB merge-patch по вложенным объектам не делается, чтобы исключить сюрпризы с частично применённой конфигурацией.
- Поле `change_reason` **обязательно** для `PATCH`, `DELETE`, `activate`, `deactivate` и `rollback`. Без него — `400 Bad Request`. Требование аудита: всегда известно, почему изменили.
- Для `POST` (создание) `change_reason` опционален: если не передан — записывается `'Initial creation'` в `config_history` для version=1.

**Rollback (`POST /api/v1/config/{id}/rollback`):**

```http
POST /api/v1/config/{id}/rollback
If-Match: "5"
Content-Type: application/json

{
  "version": 5,
  "target_version": 2,
  "change_reason": "INC-1234 — откат к последнему рабочему состоянию"
}

// Response 200 — возвращается обновлённая конфигурация с новым version (6)
```

`version` — текущая версия записи (для optimistic-lock), `target_version` — номер версии из `config_history`, к которой откатываемся. Поля разные по смыслу, поэтому переданы явно оба.

Сервис читает `config_history` по `(config_id, version=target_version)`, берёт из неё `value`, `metadata`, `is_active` и применяет как обычный `PATCH` с инкрементом `version`. Старые версии остаются в истории — повторный rollback всегда возможен.

**Dry-run (`?dry_run=true`):**
- Поддерживается на `POST /api/v1/config` и `PATCH /api/v1/config/{id}`.
- Запрос проходит все проверки (JSON-Schema, RBAC, бизнес-правила), сервис возвращает предварительный результат, но транзакция откатывается.
- Ответ: `200 OK` с `{"preview": {...}, "dry_run": true}`.
- Используется админским UI для кнопки «Проверить» перед «Создать/Сохранить».

**Оптимистическая блокировка (If-Match + `version` в теле):**

Все write-эндпоинты (`PATCH`, `DELETE`, `activate`, `deactivate`, `rollback`) **требуют** указать ожидаемую версию записи в **двух местах одновременно**:

1. HTTP-заголовок `If-Match: "<version>"` — канонично по RFC 7232, корректно работает с прокси.
2. Поле `version` в теле запроса (для `DELETE`, где тела нет, — query-параметр `?version=<N>`) — явно виден в логах, скриншотах и истории UI.

Сервер:
- При любом `GET` возвращает заголовок `ETag: "<version>"` и поле `version` в теле.
- Перед выполнением сверяет заголовок и значение в теле/query. При расхождении → `400 Bad Request` с пояснением (`version mismatch between If-Match header and request body`) — это баг клиента, не конфликт с БД.
- Если обе версии совпадают между собой, выполняет `UPDATE ... WHERE id = ? AND version = ? RETURNING *` и атомарно инкрементирует `version`. Если затронуто 0 строк → `412 Precondition Failed` (кто-то обновил запись раньше — клиент обязан перечитать и повторить).
- Отсутствие `If-Match` или `version` в теле → `428 Precondition Required`.

Дублирование сделано намеренно: заголовок закрывает HTTP-семантику, body-параметр даёт явную трассируемость в UI/CLI-логах без инспекции заголовков. `400` при расхождении между ними ловит ранние баги клиентов.

Это защищает от потерянных обновлений при параллельной работе нескольких админов и автоматизаций.

**Идемпотентность `POST`:**
- Клиент может передать заголовок `Idempotency-Key: <uuid>` на `POST /api/v1/config`.
- Сервис хранит `(idempotency_key, user_id, request_hash, response_body, response_status)` в таблице `idempotency_keys` с TTL 15 минут.
- При повторном `POST` с тем же ключом от того же пользователя в окне TTL:
  - Если `request_hash` совпадает — возвращается сохранённый ответ без повторного создания.
  - Если не совпадает — `409 Conflict` («ключ уже использован с другим телом»).
- TTL подобран под своё назначение: защита от коротких сетевых флапов и автоматических retry HTTP-клиента (секунды–минуты). Более длинные окна — это уже пользовательская логика «не создать дубль за смену», для неё есть другие механизмы (`UNIQUE (service_name, project_id, key)` и `412 Precondition Failed` на `If-Match`).
- Соответствует паттерну, принятому в experiment-service.

#### Внутренний эндпоинт для сервисов-потребителей

Сервисы платформы используют внутренний эндпоинт для получения набора конфигураций, актуальных для конкретного сервиса и проекта, с поддержкой кэширования через ETag/Last-Modified:

```
GET    /api/v1/configs/bulk?service={service_name}&project={project_id}  # Получение всех конфигураций для сервиса и проекта
```

**Параметры запроса:**
- `service` (обязательно): идентификатор сервиса (например, `experiment-service`, `telemetry-ingest`)
- `project` (опционально): идентификатор проекта, если конфигурации scoped по проектам

**Ответ:**
- JSON-объект вида `{ "configs": { "<key>": <value>, ... } }` где ключ — это имя конфигурации (`configs.key` из БД, например `write_path_enabled` или `http_qos`), а значение — поле `value` соответствующей конфигурации целиком (объект).
- Заголовок `ETag` — хеш всего набора конфигураций.
- Заголовок `Last-Modified` — timestamp самого свежего обновления среди возвращаемых конфигураций.

**Условные запросы:**
Клиент может передавать заголовок `If-None-Match` со значением предыдущего ETag. Если набор конфигураций не изменился, сервис возвращает **304 Not Modified** и пустое тело.

**Пример запроса/ответа:**

```http
GET /api/v1/configs/bulk?service=experiment-service&project=project-a
Accept: application/json
If-None-Match: "W/\"abc123\""
```

*Если конфигурации изменились:*
```http
HTTP/1.1 200 OK
Content-Type: application/json
ETag: "W/\"def456\""
Last-Modified: Thu, 17 Apr 2026 10:30:00 GMT

{
  "configs": {
    "write_path_enabled": { "enabled": true },
    "http_qos": {
      "__default__": { "timeout_ms": 150, "retries": 2 },
      "/v1/verify":  { "timeout_ms": 10000, "retries": 5 }
    }
  }
}
```

*Если конфигурации не изменились:*
```http
HTTP/1.1 304 Not Modified
ETag: "W/\"abc123\""
Last-Modified: Thu, 17 Apr 2026 10:15:00 GMT
```

**Типы конфигурации (поле `config_type`):**
- `feature_flag` — булевые переключатели функциональности; используются в том числе для экстренного отключения и graceful degradation при инцидентах.
- `qos` — параметры качества обслуживания для исходящих вызовов: таймауты и ретраи, сгруппированные по endpoint-ам, с обязательным ключом `__default__` для всех остальных путей. Группировка в одном `value` гарантирует **атомарность** связанных параметров (timeout + retries меняются одной операцией), без отдельного bulk-patch API.

> **Scope:** `rate_limit`, `circuit_breaker` и продуктовые A/B-эксперименты (rollout, сегментация по юзерам/geo) вынесены за рамки этого RFC и будут описаны в отдельных RFC по мере необходимости.

#### Валидация `value` по JSON-Schema

Для каждого `config_type` существует активная JSON-Schema (Draft 2020-12), против которой валидируется поле `value`:

- **На запись** (`POST`/`PATCH`/`rollback`) — strict: нарушение → `422 Unprocessable Entity` с описанием ошибок, запись отклоняется.
- **На чтение** (внутри сервиса — перед отдачей в bulk или single-get) — paranoid: каждая запись валидируется против текущей активной схемы, отдаётся как обычно, но при нарушении инкрементируется метрика `config_read_schema_violations_total{config_type, config_id}`. Это страховка от багов compat-checker'а: расхождения должны быть нулевыми в норме, любая ненулевая метрика — сигнал разбираться.

**Хранение схем:**

Схемы — first-class данные сервиса, лежат в таблице `config_schemas` (см. раздел «Миграции БД»). На каждый `config_type` активна ровно одна версия схемы; история версий сохраняется в той же таблице для аудита и отката.

Начальные схемы для MVP-типов (`feature_flag`, `qos`) засеиваются SQL-миграцией при первом деплое.

Пример схемы для `feature_flag`:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["enabled"],
  "properties": {
    "enabled": { "type": "boolean" }
  },
  "additionalProperties": false
}
```

Пример схемы для `qos` (per-endpoint + обязательный `__default__`):

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["__default__"],
  "additionalProperties": { "$ref": "#/$defs/qosSettings" },
  "properties": {
    "__default__": { "$ref": "#/$defs/qosSettings" }
  },
  "$defs": {
    "qosSettings": {
      "type": "object",
      "required": ["timeout_ms", "retries"],
      "properties": {
        "timeout_ms": { "type": "integer", "minimum": 1, "maximum": 600000 },
        "retries": { "type": "integer", "minimum": 0, "maximum": 10 }
      },
      "additionalProperties": false
    }
  }
}
```

Пример валидного значения:
```json
{
  "__default__":  { "timeout_ms": 150, "retries": 2 },
  "/v1/verify":   { "timeout_ms": 10000, "retries": 5 },
  "/v1/refresh":  { "timeout_ms": 300, "retries": 1 }
}
```

**Эволюция схем и обратная совместимость:**

Обновление схемы идёт через админский API (`PUT /api/v1/schemas/{config_type}`). Config-service сам решает, применять ли изменение — MVP принципиально поддерживает **только обратно-совместимые** обновления:

1. Сервис сравнивает новую схему с текущей активной (diff JSON-Schema).
2. Если все изменения — additive, создаётся новая версия, становится активной, старая остаётся в истории.
3. Если найдено хотя бы одно breaking-изменение — `422 Unprocessable Entity` с перечнем нарушений, схема НЕ обновляется.

Ожидаемая частота обновлений — раз в несколько месяцев. Ломающие изменения (с миграцией значений и отдельным тулингом) — вне scope MVP и будут описаны отдельным RFC при первой реальной потребности.

**Разрешённые (additive) изменения:**
- Добавление optional-поля.
- Расширение числовых границ (`maximum` выше, `minimum` ниже).
- Расширение `enum` (новые значения).
- Снятие `required` с поля.

**Запрещённые (breaking) изменения:**
- Добавление `required`-поля.
- Ужесточение границ (`maximum` ниже, `minimum` выше).
- Сужение `enum` (удаление значений).
- Удаление или переименование поля.
- Смена `type`.

**Дополнительная защита (sanity check):**

После прохождения compat-check и до применения новой схемы сервис в той же транзакции прогоняет новую схему по всем существующим записям `configs` этого `config_type`. При любом нарушении — транзакция откатывается, схема остаётся в старой версии, возвращается `500` с детализацией. Такой случай считается багом compat-checker'а, а не штатной ситуацией — он означает, что правила compat-проверки пропустили ломающее изменение.

**Новый `config_type`:** первая версия схемы загружается тем же эндпоинтом `PUT /api/v1/schemas/{config_type}` (compat-check не требуется, так как нет существующих записей).

> **Отложено:** автоматический экспорт изменений конфигураций (и схем) в git-репозиторий как YAML-файлов (git-mirror) — для бэкапа, истории и возможности code-review. Вынесено в отдельный RFC.

**Пример 1 — feature flag как kill-switch:**

```json
// POST /api/v1/config
{
  "service_name": "telemetry-ingest",
  "project_id": null,
  "key": "write_path_enabled",
  "config_type": "feature_flag",
  "description": "Kill-switch на приём телеметрии (использовать при инциденте)",
  "value": { "enabled": true },
  "metadata": { "owner": "sre", "ticket": "INC-1234" }
}

// Response 201
{
  "id": "a1b2c3d4-e5f6-7890-g1h2-i3j4k5l6m7n8",
  "service_name": "telemetry-ingest",
  "project_id": null,
  "key": "write_path_enabled",
  "config_type": "feature_flag",
  "description": "Kill-switch на приём телеметрии (использовать при инциденте)",
  "value": { "enabled": true },
  "metadata": { "owner": "sre", "ticket": "INC-1234" },
  "is_active": true,
  "version": 1,
  "created_at": "2026-04-17T10:00:00Z",
  "updated_at": "2026-04-17T10:00:00Z",
  "created_by": "ivan.khokhlov@company.com"
}
```

**Пример 2 — изменение QoS во время инцидента:**

```http
PATCH /api/v1/config/{id}
If-Match: "1"
Content-Type: application/json

{
  "version": 1,
  "value": {
    "__default__": { "timeout_ms": 150, "retries": 2 },
    "/v1/verify":  { "timeout_ms": 10000, "retries": 5 }
  },
  "change_reason": "Auth API тормозит на /v1/verify, INC-1234 — атомарно поднимаем timeout+retries для этой ручки"
}
```

```http
HTTP/1.1 200 OK
ETag: "2"
Content-Type: application/json

{ ... обновлённая конфигурация с "version": 2 ... }
```

Поскольку `value` заменяется целиком и содержит все endpoint'ы вместе с `__default__`, timeout и retries для конкретной ручки меняются одной атомарной операцией — промежуточное состояние типа «новый timeout, старые retries» клиенту никогда не видно.

### Интеграция с другими сервисами

**Какие сервисы вызывают новый сервис:**
- **auth-service** - проверяет доступ пользователей к изменению конфигурации (RBAC)
- **Все сервисы-platform** (experiment-service, telemetry-ingest, и т.д.) - периодически опрашивают внутренний эндпоинт `/api/v1/configs/bulk` (см. раздел «Распространение изменений» ниже)

**На какие внешние сервисы зависит:**
- **PostgreSQL** - перманентное хранилище всех конфигураций и их истории
- **auth-service** - проверка прав доступа при изменении конфигурации (через HTTP)

### Распространение изменений

Изменения применяются у сервисов-потребителей через **клиентский polling** эндпоинта `/api/v1/configs/bulk`:

- Клиент (SDK) хранит in-process кеш `{ configs, etag }`.
- Раз в `poll_interval` (по умолчанию 1 сек) делает `GET /api/v1/configs/bulk?service=<name>` с заголовком `If-None-Match: <last_etag>`.
- `304 Not Modified` → кеш остался валидным, клиент ничего не делает.
- `200 OK` → клиент атомарно заменяет кеш новым набором конфигураций и публикует событие для подписчиков внутри процесса (колбэки/observable).

**Рекомендации для SDK:**
- Jitter ±25% от `poll_interval`, чтобы несколько инстансов одного сервиса не били в config-service синхронно.
- Exponential backoff при ошибках, но продолжаем отдавать last-known-good из кеша — приложение не должно падать, если config-service недоступен.
- После каждого успешного `200 OK` SDK сохраняет ответ на локальный диск (`/var/cache/config-service/<service>.json`) как fallback-файл с атомарной записью (tmp + rename).

**Cold start и недоступность config-service (fail-open + fallback-файл):**

Приоритет источников конфигурации при старте сервиса-потребителя:

1. **ENV-override** (см. ниже) — высший приоритет, перебивает всё.
2. **config-service bulk-эндпоинт** — первичный источник.
3. **Локальный fallback-файл** (`/var/cache/config-service/<service>.json`) — используется, если config-service недоступен при cold start.
4. **Hardcoded defaults в коде** — последний рубеж; логируется `warning` о том, что работаем на дефолтах.

SDK **не блокирует старт сервиса** при недоступности config-service: применяется лучший доступный источник (fallback-файл → defaults), параллельно продолжается polling. Как только config-service ответит — конфигурация горячо обновляется, событие публикуется подписчикам. Логика: доступность сервиса важнее точности конфигов в момент старта.

**ENV-override (emergency kill-switch для config-service):**

Для каждой записи конфигурации доступна конвенция:

```
CONFIG_OVERRIDE__<SERVICE_NAME>__<KEY>=<json-value>
```

Например:
```
CONFIG_OVERRIDE__AUTH_SERVICE__WRITE_PATH_ENABLED='{"enabled":false}'
CONFIG_OVERRIDE__AUTH_SERVICE__HTTP_QOS='{"__default__":{"timeout_ms":200,"retries":0}}'
```

Если ENV-переменная выставлена, SDK **игнорирует** значение из config-service и использует ENV. Нужна для экстренного отката при каскадном инциденте (config-service перегружен / отдаёт кривой конфиг — админ выставляет ENV на нужных подах через k8s/docker/terraform и делает restart, без участия самого config-service). SDK читает ENV только при старте — перезапуск пода обязателен.

**SLO:**
- Propagation lag p95 < 5 сек (от `updated_at` в БД до применения на последнем инстансе).
- Availability config-service 99.9%; недоступность НЕ должна каскадно ронять потребителей благодаря локальному кешу.

**Ограничения голого polling:**
- Задержка применения ≈ `poll_interval` + время запроса.
- Нагрузка: `N_services × N_instances / poll_interval` RPS на config-service. При 10 сервисах × 3 инстанса × 1 сек = 30 RPS — приемлемо.
- Для мгновенного применения (SLO < 1 сек) в будущем можно добавить push (PostgreSQL `LISTEN/NOTIFY` + SSE-стрим) — но это отдельный RFC.

---

## Реализация

### Структура проекта

```
projects/backend/services/config-service/
├── src/config_service/
│   ├── api/           # REST routes (v1)
│   │   ├── v1/
│   │   │   ├── routes/
│   │   │   │   ├── config.py
│   │   │   │   └── health.py
│   │   │   └── dependencies.py
│   │   └── __init__.py
│   ├── services/      # Бизнес-логика
│   │   ├── config_service.py
│   │   ├── schema_service.py       # управление схемами, compat-check
│   │   └── validation_service.py   # валидация value против активной схемы
│   ├── repositories/  # Слой доступа к данным
│   │   ├── config_repository.py
│   │   ├── history_repository.py
│   │   └── schema_repository.py
│   ├── models/        # Pydantic модели и SQLAlchemy модели
│   │   ├── config.py
│   │   ├── history.py
│   │   ├── schema.py
│   │   └── enums.py
│   └── main.py        # Entry point
├── tests/
│   ├── unit/
│   │   ├── test_config_service.py
│   │   ├── test_schema_service.py       # compat-check, sanity-check
│   │   └── test_validation.py
│   ├── integration/                     # yandex-taxi-testsuite, реальная PG
│   │   ├── test_config_api.py
│   │   ├── test_schemas_api.py
│   │   ├── test_rbac.py
│   │   └── test_audit_and_redaction.py
│   └── conftest.py
├── migrations/        # SQL миграции (Alembic)
│   ├── versions/
│   └── env.py
├── openapi/           # OpenAPI 3.1 спецификация
│   └── config-service.yaml
├── pyproject.toml
├── Dockerfile
└── README.md
```

### Порт и переменные окружения

- **Порт:** 8004 (следовать номерации сервисов: auth-8001, experiment-8002, telemetry-ingest-8003)
- **Переменные:**
  - `DATABASE_URL`: PostgreSQL connection string (обязательно)
  - `AUTH_SERVICE_URL`: auth-service endpoint для проверки прав (обязательно)
  - `LOG_LEVEL`: уровень логирования (INFO/DEBUG/WARNING/ERROR)

### Миграции БД

Начальная схема для хранения конфигураций, scoped по сервису и проекту:

```sql
-- Основная таблица конфигураций
CREATE TABLE configs (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  service_name VARCHAR(100) NOT NULL,   -- например, 'experiment-service', 'telemetry-ingest'
  project_id VARCHAR(100),              -- nullable, если конфигурация не привязана к конкретному проекту
  key VARCHAR(255) NOT NULL,            -- имя конфигурации внутри сервиса/проекта
  config_type VARCHAR(50) NOT NULL,     -- feature_flag, qos
  description TEXT,
  value JSONB NOT NULL,                 -- Значение конфигурации (валидируется по JSON-Schema для config_type)
  metadata JSONB,                       -- Свободные метаданные (owner, ticket, комментарии)
  is_active BOOLEAN DEFAULT FALSE,
  version INTEGER DEFAULT 1,
  created_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
  created_by VARCHAR(255),              -- email или ID пользователя
  updated_by VARCHAR(255),
  deleted_at TIMESTAMPTZ,               -- soft delete: NULL = активная запись
  deleted_by VARCHAR(255),
  is_critical BOOLEAN DEFAULT FALSE,    -- конфиг с большим блэст-радиусом (kill-switch'и и т.п.); UI требует подтверждения
  is_sensitive BOOLEAN DEFAULT FALSE,   -- value содержит чувствительные данные; redacted в audit-лог и для не-админов
  UNIQUE NULLS NOT DISTINCT (service_name, project_id, key)  -- PG 15+: NULL в project_id трактуется как равный NULL, что блокирует дубликаты для глобальных конфигов
);

-- Хранилище JSON-Schema по config_type (runtime source of truth для валидатора)
CREATE TABLE config_schemas (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  config_type VARCHAR(50) NOT NULL,      -- feature_flag, qos
  version INTEGER NOT NULL,              -- инкрементируется при каждом обновлении
  schema JSONB NOT NULL,                 -- JSON Schema Draft 2020-12
  is_active BOOLEAN DEFAULT FALSE,       -- ровно одна активная версия на config_type
  created_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
  created_by VARCHAR(255),
  UNIQUE (config_type, version)
);

-- Гарантирует ровно одну активную версию на каждый config_type
CREATE UNIQUE INDEX idx_config_schemas_active_unique
  ON config_schemas(config_type) WHERE is_active = TRUE;

-- Начальный seed схем MVP-типов (feature_flag, qos) — заполняется миграцией.

-- История изменений конфигураций (для аудита и отката)
CREATE TABLE config_history (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  config_id UUID NOT NULL REFERENCES configs(id) ON DELETE NO ACTION,  -- защищаем историю от потери при случайном hard delete
  service_name VARCHAR(100) NOT NULL,
  project_id VARCHAR(100),
  key VARCHAR(255) NOT NULL,
  config_type VARCHAR(50) NOT NULL,
  description TEXT,
  value JSONB NOT NULL,
  metadata JSONB,
  is_active BOOLEAN,
  version INTEGER,
  deleted_at TIMESTAMPTZ,            -- snapshot значения на момент изменения
  is_critical BOOLEAN,
  is_sensitive BOOLEAN,
  changed_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
  changed_by VARCHAR(255),
  source_ip VARCHAR(45),             -- IP клиента (IPv6 max 45 символов)
  user_agent TEXT,                   -- источник: UI / CLI / автоматика
  correlation_id VARCHAR(64),        -- trace-id из OpenTelemetry для связи с логами
  change_reason TEXT                 -- Почему было сделано изменение
);

-- Индексы для быстрого поиска
CREATE INDEX idx_configs_service ON configs(service_name);
CREATE INDEX idx_configs_project ON configs(project_id);
CREATE INDEX idx_configs_key ON configs(key);
CREATE INDEX idx_configs_type ON configs(config_type);
CREATE INDEX idx_configs_active ON configs(is_active) WHERE is_active = TRUE AND deleted_at IS NULL;
CREATE INDEX idx_configs_not_deleted ON configs(deleted_at) WHERE deleted_at IS NULL;
CREATE INDEX idx_config_history_config_id ON config_history(config_id);
CREATE INDEX idx_config_history_changed_at ON config_history(changed_at);

-- Таблица для идемпотентности POST-запросов
CREATE TABLE idempotency_keys (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  idempotency_key VARCHAR(255) NOT NULL,
  user_id VARCHAR(255) NOT NULL,
  request_hash VARCHAR(64) NOT NULL,     -- sha256 тела запроса (для проверки совпадения)
  response_body JSONB NOT NULL,
  response_status INTEGER NOT NULL,
  created_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
  expires_at TIMESTAMPTZ NOT NULL,       -- created_at + 15 минут
  UNIQUE (idempotency_key, user_id)
);

CREATE INDEX idx_idempotency_keys_expires ON idempotency_keys(expires_at);
-- Очистка истёкших ключей: периодический cron-job либо pg_cron (`DELETE FROM idempotency_keys WHERE expires_at < NOW()`).

-- Триггер для автоматического обновления updated_at
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = CURRENT_TIMESTAMP;
  RETURN NEW;
END;
$$ language 'plpgsql';

CREATE TRIGGER update_configs_updated_at
BEFORE UPDATE ON configs
FOR EACH ROW EXECUTE PROCEDURE update_updated_at_column();

-- История изменений (config_history) заполняется из Python-репозитория (config_repository)
-- в той же транзакции, что и INSERT/UPDATE на configs. Триггер не используется —
-- change_reason передаётся параметром метода репозитория, что избегает session-level
-- GUC-переменных и делает поток данных явным в коде.
--
-- Репозиторий пишет в config_history на каждую запись, включая INSERT (version=1):
-- начальная запись нужна для полноты аудита и чтобы rollback мог возвращаться к version=1.
-- change_reason для POST опционален (default 'Initial creation');
-- для PATCH, DELETE (soft), activate, deactivate, rollback — обязателен (API возвращает 400, если не передан).
```

---

## Безопасность

### Защита bulk-эндпоинта

Bulk-эндпоинт `GET /api/v1/configs/bulk` используется всеми сервисами-потребителями и содержит полный набор актуальных конфигов. В MVP защита — **сетевая изоляция**: эндпоинт биндится только на internal network (Yandex Cloud VPC / внутренняя сеть docker-compose), не выставляется через ingress / API gateway.

Подделка запроса возможна только при проникновении во внутреннюю сеть — модель угроз уровня ниже, чем у публичных эндпоинтов.

> **Отложено в отдельный RFC:** service-to-service аутентификация (у каждого сервиса-потребителя собственный токен; параметр `?service=<name>` проверяется против токена — несовпадение → `403`). Нужно будет реализовать перед выводом за пределы одной команды или при переходе на multi-tenant.

### RBAC

Используется существующая RBAC-инфраструктура auth-service. Матрица ролей config-service:

| Роль | Права |
|------|-------|
| `config_admin` | Полный доступ: CRUD по всем `service_name`, управление схемами (`PUT /api/v1/schemas/*`), чтение sensitive-значений |
| `config_editor:{service_name}` | CRUD, activate/deactivate/rollback только для конфигов указанного `service_name` |
| `config_viewer` | Только чтение через UI (`GET /api/v1/config*`). Bulk-эндпоинт защищён сетевой изоляцией, отдельная роль не требуется |

На каждый write-запрос config-service вызывает `POST /authorize` у auth-service с `(user_id, action, resource={service_name, config_id})` и ждёт `200 OK`. Несовпадение → `403 Forbidden`.

Обновление схем (`PUT /api/v1/schemas/{config_type}`) доступно только `config_admin`.

### Audit log

Кроме полей `changed_by` / `change_reason`, `config_history` содержит:
- `source_ip` — IP клиента (поддерживается IPv6).
- `user_agent` — источник запроса (UI / CLI / автоматика).
- `correlation_id` — trace-id из OpenTelemetry.

Параллельно каждое изменение пишется в Loki (через Alloy) как structured-JSON с полями `service`, `config_type`, `config_id`, `actor`, `action`, `change_reason`, `is_critical`, `correlation_id`, `source_ip`. Это даёт:
- фильтрацию и дашборды по актору / сервису в Grafana,
- алерты на подозрительные паттерны (например, >10 изменений критичных конфигов за минуту),
- быстрый grep во время инцидентов.

### Критичные конфигурации

Поле `is_critical BOOLEAN DEFAULT FALSE` в `configs`. Используется для kill-switch'ей и переключателей с большим блэст-радиусом.

- Помечается админом вручную при создании или отдельным PATCH.
- При изменении `is_critical=TRUE`-конфига админский UI требует явного подтверждения: checkbox «Я понимаю последствия» + повтор `key` или расширенный `change_reason` (≥20 символов).
- Флаг возвращается в API-ответах, чтобы UI знал, когда показывать предупреждение.

> **Отложено:** 4-eyes approval (согласование от второго админа для изменения критичного конфига) и rate limit на write-операции — оба требуют отдельной инфраструктуры, уходят в backlog.

### Sensitive values

Поле `is_sensitive BOOLEAN DEFAULT FALSE` в `configs`. Указывает, что `value` содержит чувствительные данные.

- В structured-audit-log (Loki) `value` для sensitive-конфигов заменяется на `"***"` перед записью.
- В API-ответах для ролей ниже `config_admin` поле `value` также заменяется на `"***"`.
- В `config_history` `value` хранится как есть (нужно для отката), доступ к нему — только для `config_admin` через явный параметр `?include_sensitive=true`.

> **Отложено в отдельный RFC:** полноценный secrets management. Два варианта на будущее — (а) отдельный **secrets-service** поверх Yandex Lockbox / Vault, либо (б) расширение config-service reference'ами на внешние хранилища (`vault://path/...`), резолвимыми на стороне bulk-эндпоинта. Выбор — когда появится реальная потребность в runtime-секретах.

---

## Тестирование

Все тесты, которым нужна БД, гоняются на реальном PostgreSQL через **yandex-taxi-testsuite** (как в остальных сервисах платформы). In-memory SQLite и моки БД не используются: `UNIQUE NULLS NOT DISTINCT`, JSONB, partial-indexы и оптимистическая блокировка на `UPDATE ... WHERE version = ?` воспроизводимы только на реальной PG, а моки дают ложную зелёную сборку. Моки используются только на границах, которые в тестах не нужны — например, HTTP-клиент к auth-service.

### Unit tests

- Бизнес-логика `config_service` и `schema_service` (создание/обновление/активация, decision-tree rollback).
- `validation_service`: валидация `value` против активной JSON-Schema для каждого `config_type` (позитивные/негативные кейсы, граничные значения для `qos`).
- Compat-checker схем: матрица additive-изменений (разрешены) и breaking-изменений (запрещены) — по одному unit-тесту на каждое правило из раздела «Эволюция схем».
- Sanity-check после compat-check: отдельный тест, где compat-checker по ошибке пропускает ломающее изменение — должен откатить транзакцию и вернуть `500` с детализацией.
- Pydantic-модели: валидация форматов входных данных, обязательность `change_reason` для `PATCH`/`DELETE`/`activate`/`deactivate`/`rollback`.
- Сериализация/redaction sensitive-значений: для не-админов `value` → `"***"`, в structured-logs тоже `"***"`.

### Integration tests (yandex-taxi-testsuite + реальная PG)

- **CRUD-сценарий полным циклом:** создание → чтение → `PATCH` → soft delete → проверка отсутствия в `list`/`bulk` → проверка наличия в `history`.
- **Rollback:** создать версии 1→2→3, `POST /rollback` c `target_version=1`, убедиться, что `version=4` и `value` соответствует снапшоту version=1; повторный rollback к version=3 после этого успешен.
- **Dry-run:** `POST`/`PATCH` с `?dry_run=true` — ответ `200 OK {dry_run: true}`, транзакция откачена, в `configs` и `config_history` ничего не записано.
- **Idempotency-Key:** повтор того же `POST` с тем же ключом и телом → тот же `201` с тем же `id`; повтор с другим телом → `409 Conflict`; запись от другого `user_id` с тем же ключом → создаётся новая.
- **Оптимистическая блокировка (If-Match + body-version):** параллельный `PATCH` от двух клиентов с одинаковым `If-Match: "N"` и `"version": N` в теле — первый получает `200` и `version=N+1`, второй получает `412 Precondition Failed`; запрос без `If-Match` или без `version` в теле → `428 Precondition Required`; запрос с расхождением `If-Match: "3"` и `"version": 2` в теле → `400 Bad Request`.
- **Soft delete:** `DELETE` выставляет `deleted_at`/`deleted_by`, запись исчезает из `GET /list` и `/bulk`, остаётся в `GET /history`; повторный `POST` того же `(service, project, key)` создаёт новую запись (учитывая `UNIQUE NULLS NOT DISTINCT` вместе с `deleted_at IS NULL`-индексом — уточнить при реализации, что uniqueness-ограничение работает корректно для soft-deleted).
- **Audit-поля:** в `config_history` пишутся `source_ip`, `user_agent`, `correlation_id` (prop'нутый из OpenTelemetry); в Loki уходит structured-JSON с теми же полями.
- **Compat-checker на реальных данных:** загружаем схему, создаём записи, пытаемся обновить схему additive — успех; пытаемся обновить breaking — `422`, схема не меняется.
- **Paranoid read-validation:** руками подменяем `value` в БД на ломающее схему значение, делаем `GET /bulk` — ответ приходит как обычно, метрика `config_read_schema_violations_total{config_type, config_id}` инкрементируется.
- **RBAC по матрице:** `config_admin` — всё; `config_editor:auth-service` — CRUD только в своём `service_name`, `403` на чужой; `config_viewer` — только `GET /config*`, `403` на `PUT /schemas`; bulk-эндпоинт в integration-окружении доступен без токена (сетевая изоляция).
- **Sensitive redaction:** создаём `is_sensitive=true`-конфиг, проверяем что `config_viewer` видит `"***"`, `config_admin` видит значение; в Loki-логе `value` заменён на `"***"`; `GET /history?include_sensitive=true` для не-админа → `403`.
- **ETag на `/bulk`:** два запроса подряд с `If-None-Match` → второй `304 Not Modified`; после `PATCH` — `200 OK` с новым ETag.

### Load tests

Отложены: есть отдельная задача на постройку удобного load-тест-тулинга («стрельбы»). После её проработки — добавить сценарии (propagation lag под polling-нагрузкой, write-contention на одном `config_id`, планка RPS на `/bulk`) в этот RFC и проект тестов config-service.

---

## Развёртывание

### Docker Compose (dev)

```yaml
config-service:
  image: config-service:latest
  ports:
    - "8004:8004"
  environment:
    DATABASE_URL: postgresql://user:pass@postgres:5432/config_service_db
    AUTH_SERVICE_URL: http://auth-service:8001
    LOG_LEVEL: INFO
  depends_on:
    - postgres
```

### Миграции в CI/CD

```bash
# При деплое на stage/prod
make config-service-migrate
```

### Observability

**Трейсинг и логи:**
- OpenTelemetry (OTLP) для распределённой трассировки. `correlation_id` из трейса пишется в `config_history` — аудит связывается с логами напрямую.
- Логирование в Loki через Alloy. Уровень `INFO` в prod, `DEBUG` по необходимости. Structured-JSON для каждого изменения конфигурации (см. раздел «Безопасность → Audit log»).

**Метрики (Prometheus):**

RED по API:
- `config_http_requests_total{route, method, status}` — счётчик запросов.
- `config_http_request_duration_seconds{route}` — histogram задержки.

Доменные:
- `config_propagation_lag_seconds{service_name}` — histogram времени от `updated_at` в БД до применения в SDK. Замер делает SDK; основной SLO-индикатор.
- `config_read_schema_violations_total{config_type, config_id}` — срабатывания paranoid read-validation (см. раздел «Валидация `value`»). В норме 0.
- `config_compat_check_rejections_total{config_type, rule}` — сколько breaking-попыток обновить схему отбраковано, с указанием нарушенного правила.
- `config_sanity_check_failures_total{config_type}` — срабатывания sanity-check после compat-check. Ненулевое значение = баг в compat-checker'е.
- `config_idempotency_hits_total{result}` — `returned_cached` / `conflict`.
- `config_optimistic_lock_conflicts_total{route}` — количество `412 Precondition Failed`.
- `config_audit_log_writes_total{action, is_critical}` — число изменений с разбивкой по критичности.
- `config_bulk_responses_total{status}` — `200` / `304`, индикатор эффективности ETag-кеширования.

**Алерты (Grafana):**

| Важность | Условие | Интерпретация |
|----------|---------|---------------|
| P1 | `config_sanity_check_failures_total > 0` (любое окно) | Compat-checker пропустил ломающее изменение — баг в сервисе |
| P1 | Availability config-service < 99% за 5 мин | Сервис недоступен для админов |
| P2 | `config_propagation_lag_seconds` p95 > 5s за 10 мин | Нарушение SLO распространения |
| P2 | `config_read_schema_violations_total > 0` за 10 мин | Данные разошлись со схемой |
| P2 | Более 10 изменений с `is_critical=true` за 1 мин | Подозрительная активность / инцидент |
| P3 | Рост `config_optimistic_lock_conflicts_total` | Несколько админов одновременно правят один конфиг |

Пороги SLO — стартовые, донастраиваются после первых недель эксплуатации.

### Бэкап БД

Резервное копирование `configs`/`config_schemas`/`config_history` — **через управляемый PostgreSQL в Yandex Cloud** (встроенный PITR). Параметры RPO/RTO и политика снапшотов задаются в Terraform вместе с остальными БД проекта. Вопрос детальных требований (в частности — нужна ли config-service более частая частота снапшотов, чем остальным БД из-за критичности для инцидент-респонса) выносим в отдельную операционную задачу при настройке продакшен-окружения.

### Rollback самого сервиса

Откат плохой версии config-service отделён от rollback конфигов (`POST /api/v1/config/{id}/rollback`) и устроен так:

- **Код:** стандартный откат Docker-образа через CI/CD (теги версий, как у остальных сервисов).
- **Миграции БД:** Alembic `downgrade` для **каждого** шага. Любая миграция, создающая колонку или индекс, обязана иметь рабочий `downgrade`.
- **Двухфазный деплой для drop-миграций:** миграции вида «удалить колонку / таблицу» пишутся в два релиза:
  1. Релиз N: приложение перестаёт читать/писать колонку, но колонка остаётся в БД.
  2. Релиз N+1: миграция удаляет колонку.

  Это позволяет откатить код c релиза N+1 обратно на N без потери данных и без восстановления из бэкапа.
- **Испорченные данные:** если плохая версия успела записать мусор в `configs`/`config_schemas` — точечный откат через `POST /api/v1/config/{id}/rollback` на конкретных `config_id` (используя `config_history` для восстановления корректного снапшота). Массовая порча → восстановление через PITR Yandex Cloud.

---

## Альтернативы

Почему выбран этот подход вместо других?

- **Альтернатива 1: Использование только переменных окружения и ConfigMaps** - Описание: Хранить всю конфигурацию в переменных окружения или Kubernetes ConfigMaps. Плюсы: простота реализации, нет внешних зависимостей. Минусы: требует перезапуска сервисов при изменении, сложно управлять условиями и версионированием, нет истории изменений.

- **Альтернатива 2: Сторонние решения (Consul, etcd, Apache Zookeeper)** - Описание: Использовать специализированные системы для distributed конфигурации. Плюсы: готовые решения, поддержка наблюдения за изменениями, сильная консистентность. Минусы: дополнительная сложность в инфраструктуре, требуется изучение новой технологии, потенциальный vendor lock-in.

- **Выбранный подход:** собственный сервис на основе PostgreSQL лучше, потому что:
  1. Использует уже существующую в стеке технологию (PostgreSQL), без дополнительных зависимостей (Redis, RabbitMQ не требуются для MVP)
  2. Позволяет гибко расширять типы конфигурации через JSON-Schema в репозитории, проходя code review на каждый новый тип
  3. Обеспечивает полный контроль над производительностью и оптимизацией
  4. Легко интегрируется с существующими системами мониторинга и алертинга
  5. Обеспечивает историю изменений и аудит из коробки

---

## Известные ограничения / Tech Debt

Явно отложенные пункты, которые войдут в отдельные RFC или задачи по мере необходимости:

- **Распространение изменений — только polling.** Propagation lag p95 ≈ `poll_interval` + round-trip. Для SLO < 1 сек нужна push-модель (PostgreSQL `LISTEN/NOTIFY` + SSE-стрим до SDK). Отдельный RFC.
- **Service-to-service аутентификация для bulk-эндпоинта.** В MVP — сетевая изоляция (internal VPC). Токены на каждый сервис-потребитель с проверкой по `?service=<name>` — отложено до вывода за пределы одной команды или multi-tenant.
- **Git-mirror конфигураций и схем.** Автоматический экспорт изменений в git-репозиторий как YAML-файлов (бэкап, history, code-review на изменения критичных конфигов). Отдельный RFC.
- **Secrets management.** Для `is_sensitive` в MVP делается только redaction в логах и API для не-админов. Полноценное хранилище (отдельный secrets-service поверх Yandex Lockbox / Vault, либо reference-формат `vault://path` в `value`) — отдельный RFC при реальной потребности в runtime-секретах.
- **4-eyes approval на критичные конфиги.** Согласование от второго админа для изменения `is_critical=true`-конфигов. Требует UI-инфраструктуры (approvals, уведомления) — в backlog.
- **Rate limit на write-операции.** Сейчас защиты от админа-который-залип-на-кнопке нет. После появления общей rate-limit-инфраструктуры включить для `POST`/`PATCH`.
- **Breaking-изменения схем.** MVP принципиально отклоняет breaking-изменения. Инструментарий для миграции значений под ломающие изменения схемы — отдельный RFC при первой реальной потребности.
- **`rate_limit` / `circuit_breaker` / A/B-эксперименты как `config_type`.** Вынесены из scope RFC, реализуются отдельными RFC по мере необходимости.
- **Load testing.** См. раздел «Тестирование → Load tests»: ждём готовности общего load-тест-тулинга («стрельбы»).

Технические ограничения, которые стоит документировать для пользователей сервиса:

- **Размер `value`.** Поле `JSONB` в PG имеет практический лимит ~1 GB, но большие конфигурации влияют на производительность `/bulk` и polling. Рекомендуемый максимум на одну запись — десятки килобайт; мониторим через histogram размера ответа `/bulk`.
- **ENV-override требует рестарта пода.** SDK читает `CONFIG_OVERRIDE__*` только при старте, горячая подмена ENV не поддерживается (by design — чтобы ENV оставался предсказуемым emergency-switch).

---

## План реализации

### Phase 1: MVP (неделя 1)

- [ ] Структура проекта + Dockerfile
- [ ] БД миграции (таблицы `configs`, `config_history`, `config_schemas`, `idempotency_keys`)
- [ ] Seed начальных схем MVP-типов (`feature_flag`, `qos`) через миграцию
- [ ] API базовые эндпоинты: CRUD, фильтры, cursor-pagination, soft delete
- [ ] Rollback-эндпоинт (`POST /api/v1/config/{id}/rollback`)
- [ ] Dry-run (`?dry_run=true`) для POST/PATCH
- [ ] Оптимистическая блокировка через `If-Match` / `ETag` на всех write-эндпоинтах
- [ ] Idempotency-Key middleware + очистка истёкших ключей
- [ ] Audit-поля в `config_history` (`source_ip`, `user_agent`, `correlation_id`) + `is_critical` / `is_sensitive` в `configs`
- [ ] Structured audit-логирование изменений в Loki (redaction sensitive-значений)
- [ ] JSON-Schema валидация `value`: strict на запись, paranoid на чтение с метрикой `config_read_schema_violations_total`
- [ ] API управления схемами (`GET/PUT /api/v1/schemas`) с compat-checker'ом (additive-only) + sanity-check по существующим записям
- [ ] Unit tests: бизнес-логика, валидация, compat-check на матрицу additive/breaking правил, sanity-check

### Phase 2: Интеграция (неделя 2)

- [ ] Интеграция с auth-service: RBAC matrix (`config_admin`, `config_editor:{service}`, `config_viewer`)
- [ ] Redaction sensitive-значений в API-ответах для не-админов (и запрет `?include_sensitive=true` не-админам)
- [ ] Bulk-эндпоинт `GET /api/v1/configs/bulk` c ETag / `If-None-Match`
- [ ] Сетевая изоляция bulk-эндпоинта (Yandex Cloud VPC / docker-compose networks)
- [ ] Integration tests на yandex-taxi-testsuite: CRUD, rollback, dry-run, idempotency, optimistic locking, soft delete, audit, compat + sanity, paranoid-read, RBAC, redaction, ETag `/bulk`
- [ ] OpenAPI 3.1 спецификация
- [ ] Health check эндпоинт

### Phase 3: Production (неделя 3)

- [ ] SDK для сервисов-потребителей: polling с jitter, ETag-кеш, fallback-файл, ENV-override, cold-start-логика
- [ ] OTLP/Loki конфигурация (трассировка и логирование)
- [ ] Метрики из раздела «Observability»: RED по API, `propagation_lag`, `read_schema_violations`, `compat_check_rejections`, `sanity_check_failures`, `idempotency_hits`, `optimistic_lock_conflicts`, `audit_log_writes`, `bulk_responses`
- [ ] Алерты в Grafana по таблице из раздела «Observability»
- [ ] Managed PostgreSQL в Yandex Cloud (PITR — через инфраструктурный Terraform, общий с остальными БД)
- [ ] Documentation: руководство для сервисов-потребителей (как подключить SDK, ENV-override в инциденте)
- [ ] Security audit: корректность авторизации по матрице, отсутствие утечек sensitive в ответах и логах, корректность redaction в `config_history`

---

## Ссылки

- Related ADR: [docs/adr/XXXX-название.md](../adr/)
- Issue/Epic: [GitHub issue #XXX](https://github.com/)
- Slack thread: (если есть обсуждение)

---

## История изменений

| Дата | Автор | Изменение |
|------|-------|----------|
| 2026-04-17 | Ivan Khokhlov | Первая версия |
| 2026-04-20 | Ivan Khokhlov | Доработка после ревью: scope сужен до incident response (убраны A/B и rate_limit), Redis/воркер/RabbitMQ убраны, введены `qos` и `feature_flag`, JSON-Schema с `config_schemas` и additive-only эволюцией, оптимистическая блокировка (`If-Match`), idempotency, dry-run, soft delete, rollback-эндпоинт, RBAC-матрица, audit-поля, `is_critical` / `is_sensitive`, fallback-файл + ENV-override, paranoid read-validation, observability и план тестов на yandex-taxi-testsuite |
| 2026-04-20 | Ivan Khokhlov | Idempotency TTL снижен с 24h до 15 мин (механизм предназначен для защиты от сетевых флапов, не длинных окон). Для optimistic-lock дополнительно к `If-Match` требуется дублировать `version` в теле запроса (или `?version=` для `DELETE`); расхождение между заголовком и телом → `400` |
