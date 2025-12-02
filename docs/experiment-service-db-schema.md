# Experiment Service Database Schema (Foundation)

Этот документ описывает минимальную схему PostgreSQL, необходимую для выполнения блока «1. Foundation» из дорожной карты. На этом этапе мы концентрируемся на CRUD управлении экспериментами, запусками и capture session, а также на базовой поддержке датчиков и профилей преобразования, чтобы дальнейшие итерации (метрики, артефакты, телеметрия) строились на уже согласованных структурах.

## Ключевые принципы

- **Мультипроектность:** каждая сущность содержит `project_id` и защищена внешними ключами/уникальными индексами, чтобы запросы всегда фильтровались по проекту пользователя.
- **UUID везде:** первичные ключи — `UUID` (`gen_random_uuid()`), чтобы упрощать синхронизацию между сервисами.
- **Временные метки:** `created_at`/`updated_at` хранятся в `timestamptz` и обновляются через триггер `set_updated_at`.
- **JSONB для нерегламентированных полей:** параметры запусков, произвольные метаданные эксперимента, полезная нагрузка профилей преобразования.
- **Аудит и идемпотентность:** таблицы `request_idempotency` и `capture_session_events` позволяют реализовать требования по повторным запросам и логированию действий.

## Доменные ENUM-типы

```sql
CREATE TYPE experiment_status AS ENUM ('draft', 'running', 'failed', 'succeeded', 'archived');
CREATE TYPE run_status AS ENUM ('draft', 'running', 'failed', 'succeeded', 'archived');
CREATE TYPE capture_session_status AS ENUM ('draft', 'running', 'failed', 'succeeded', 'archived', 'backfilling');
CREATE TYPE sensor_status AS ENUM ('registering', 'active', 'inactive', 'decommissioned');
CREATE TYPE conversion_profile_status AS ENUM ('draft', 'active', 'scheduled', 'deprecated');
```

## Таблицы

### `experiments`

| Колонка | Тип | Ограничения | Описание |
| --- | --- | --- | --- |
| `id` | `uuid` | PK, default `gen_random_uuid()` | Идентификатор эксперимента. |
| `project_id` | `uuid` | NOT NULL | Ссылка на проект из Auth Service. |
| `owner_id` | `uuid` | NOT NULL | Кто создал/владеет экспериментом. |
| `name` | `text` | NOT NULL | Уникально в рамках проекта. |
| `description` | `text` | NULL | Markdown/текст описания. |
| `experiment_type` | `text` | NULL | Тип/класс эксперимента. |
| `tags` | `text[]` | NOT NULL DEFAULT `'{}'::text[]` | Для быстрого фильтра. |
| `metadata` | `jsonb` | NOT NULL DEFAULT '{}'::jsonb | Доп. атрибуты UI. |
| `status` | `experiment_status` | NOT NULL DEFAULT 'draft' | Текущий статус. |
| `archived_at` | `timestamptz` | NULL | Время архивирования. |
| `created_at` | `timestamptz` | NOT NULL DEFAULT now() | |
| `updated_at` | `timestamptz` | NOT NULL DEFAULT now() | |

Дополнительно:
- `UNIQUE (project_id, lower(name))` — защита от дублей.
- `GIN (tags)` и `GIN (metadata)` для фильтрации.
- `FOREIGN KEY (owner_id)` будет ссылаться на таблицу пользователей Auth Service после интеграции; пока можно ограничиться проверкой существования через сервис.

### `runs`

| Колонка | Тип | Ограничения | Описание |
| --- | --- | --- | --- |
| `id` | `uuid` | PK | |
| `experiment_id` | `uuid` | NOT NULL | FK → `experiments(id)` |
| `project_id` | `uuid` | NOT NULL | Дублируем для быстрого scope-фильтра. |
| `created_by` | `uuid` | NOT NULL | Автор запуска. |
| `name` | `text` | NULL | Отображаемое имя (опционально). |
| `params` | `jsonb` | NOT NULL DEFAULT '{}'::jsonb | Входные параметры/конфиги. |
| `git_sha` | `text` | NULL | Привязка к репозиторию. |
| `env` | `text` | NULL | Название окружения. |
| `status` | `run_status` | NOT NULL DEFAULT 'draft' | |
| `started_at` | `timestamptz` | NULL | |
| `finished_at` | `timestamptz` | NULL | |
| `duration_seconds` | `integer` | NULL | Кэш длительности. |
| `notes` | `text` | NULL | Комментарии. |
| `metadata` | `jsonb` | NOT NULL DEFAULT '{}'::jsonb | Для UI. |
| `created_at` / `updated_at` | `timestamptz` | NOT NULL DEFAULT now() | |

Ограничения:
- `FOREIGN KEY (experiment_id, project_id)` → `experiments(id, project_id)` (потребует `UNIQUE (id, project_id)` в `experiments`) — гарантирует, что запуск не может ссылаться на эксперимент другого проекта.
- Индекс `idx_runs_project_status` на `(project_id, status)` для списков.
- Индекс `idx_runs_git_sha` на `(git_sha)` для поиска по коммитам.

### `run_sensors`

Матрица «какие датчики участвуют в запуске и в каком режиме».

| Колонка | Тип | Описание |
| --- | --- | --- |
| `run_id` | `uuid` | FK → `runs(id)` |
| `sensor_id` | `uuid` | FK → `sensors(id)` |
| `project_id` | `uuid` | NOT NULL, должен совпадать с `runs.project_id`. |
| `mode` | `text` | Например, `primary`, `reference`, `diagnostic`. |
| `attached_at` | `timestamptz` | когда привязали. |
| `detached_at` | `timestamptz` | когда отвязали. |
| `created_by` | `uuid` | Пользователь, назначивший датчик. |

PK: `(run_id, sensor_id)`. Индекс `(sensor_id, mode)` для обратного поиска.

### `capture_sessions`

| Колонка | Тип | Ограничения | Описание |
| --- | --- | --- | --- |
| `id` | `uuid` | PK | |
| `run_id` | `uuid` | NOT NULL | FK → `runs(id)` |
| `project_id` | `uuid` | NOT NULL | Scope/ RBAC. |
| `ordinal_number` | `integer` | NOT NULL | `1..n` в рамках run. |
| `status` | `capture_session_status` | NOT NULL DEFAULT 'draft' | |
| `initiated_by` | `uuid` | NULL | Кто нажал «Старт». |
| `notes` | `text` | NULL | Комментарии. |
| `started_at` | `timestamptz` | NULL | |
| `stopped_at` | `timestamptz` | NULL | |
| `archived` | `boolean` | NOT NULL DEFAULT false | Soft delete для UI. |
| `created_at` / `updated_at` | `timestamptz` | NOT NULL DEFAULT now() | |

Ограничения:
- `UNIQUE (run_id, ordinal_number)` — упорядочивание сессий.
- `FOREIGN KEY (run_id, project_id)` → `runs(id, project_id)` — тот же проект.

### `capture_session_events`

Логирование start/stop/backfill с привязкой к пользователю, как требует раздел 6.1 ТЗ.

| Колонка | Тип | Описание |
| --- | --- | --- |
| `id` | `bigserial` | PK |
| `capture_session_id` | `uuid` | FK → `capture_sessions(id)` |
| `event_type` | `text` | `started`, `stopped`, `backfill_started`, `backfill_completed`, `deleted` и т.д. |
| `actor_id` | `uuid` | Пользователь, вызвавший событие. |
| `actor_role` | `text` | owner/editor/viewer |
| `payload` | `jsonb` | Доп. данные (причина остановки, idempotency key). |
| `created_at` | `timestamptz` | DEFAULT now() |

### `sensors`

| Колонка | Тип | Описание |
| --- | --- | --- |
| `id` | `uuid` | PK |
| `project_id` | `uuid` | NOT NULL |
| `name` | `text` | NOT NULL, уникально в проекте |
| `type` | `text` | Температура, ток и т.п. |
| `input_unit` | `text` | Единица исходного сигнала |
| `display_unit` | `text` | Что показываем в UI |
| `status` | `sensor_status` | DEFAULT 'registering' |
| `token_hash` | `bytea` | Храним bcrypt/argon hash токена |
| `token_preview` | `text` | Последние 4 символа для UI |
| `last_heartbeat` | `timestamptz` | NULL |
| `active_profile_id` | `uuid` | FK → `conversion_profiles(id)` |
| `calibration_notes` | `text` | NULL |
| `created_at` / `updated_at` | `timestamptz` | DEFAULT now() |

Индексы:
- `UNIQUE (project_id, lower(name))`
- `idx_sensors_project_status` на `(project_id, status)`

### `conversion_profiles`

| Колонка | Тип | Описание |
| --- | --- | --- |
| `id` | `uuid` | PK |
| `sensor_id` | `uuid` | FK → `sensors(id)` |
| `project_id` | `uuid` | NOT NULL |
| `version` | `text` | Например `v1`, `v2`, уникально в рамках датчика |
| `kind` | `text` | `linear`, `table`, `custom` |
| `payload` | `jsonb` | Коэффициенты/таблицы |
| `status` | `conversion_profile_status` | DEFAULT 'draft' |
| `valid_from` / `valid_to` | `timestamptz` | NULL |
| `created_by` | `uuid` | NOT NULL |
| `published_by` | `uuid` | NULL |
| `created_at` / `updated_at` | `timestamptz` | DEFAULT now() |

Ограничения:
- `UNIQUE (sensor_id, version)`
- `FOREIGN KEY (sensor_id, project_id)` → `sensors(id, project_id)`

### `artifacts`

| Колонка | Тип | Описание |
| --- | --- | --- |
| `id` | `uuid` | PK |
| `run_id` | `uuid` | FK → `runs(id)` |
| `project_id` | `uuid` | NOT NULL |
| `type` | `text` | model/log/plot/etc |
| `uri` | `text` | Ссылка на объект в S3/MinIO |
| `checksum` | `text` | SHA256 |
| `size_bytes` | `bigint` | Размер |
| `metadata` | `jsonb` | Доп. сведения (mime-type, labels) |
| `created_by` | `uuid` | Автор |
| `approved_by` | `uuid` | Кто промотировал |
| `approval_note` | `text` | Комментарий |
| `is_restricted` | `boolean` | Ограниченные артефакты |
| `created_at` / `updated_at` | `timestamptz` | DEFAULT now() |

### `request_idempotency`

Хранилище ключей идемпотентности для REST-запросов create/update.

| Колонка | Тип | Описание |
| --- | --- | --- |
| `idempotency_key` | `text` | PK (hash ключа) |
| `user_id` | `uuid` | Кто сделал запрос |
| `request_path` | `text` | `/api/v1/experiments` |
| `request_body_hash` | `bytea` | SHA256 тела |
| `response_status` | `integer` | HTTP код |
| `response_body` | `jsonb` | Фрагмент ответа |
| `created_at` | `timestamptz` | DEFAULT now() |

API при получении повторного запроса с тем же ключом возвращает сохранённый ответ.

### `schema_migrations`

Техническая таблица для SQL-миграций.

| Колонка | Тип | Описание |
| --- | --- | --- |
| `version` | `text` | Первичный ключ, совпадает с названием файла миграции (`001_initial_schema`). |
| `checksum` | `text` | SHA256 содержимого файла; позволяет обнаруживать ручные правки. |
| `applied_at` | `timestamptz` | Время применения. |

Скрипт `backend/services/experiment-service/bin/migrate.py` последовательно читает `.sql` файлы из каталога `migrations/`, прогоняет их в транзакции и записывает результат в `schema_migrations`. При запуске с флагом `--dry-run` он просто выводит список ожидающих миграций.

## Диаграмма связей (упрощённо)

```
projects (external)
    |
experiments ──< runs ──< capture_sessions
      \            \
       \            └─< run_sensors >── sensors ──< conversion_profiles
        \
         └─< artifacts
```

## Следующие шаги

1. Завести Alembic и описать перечисленные типы/таблицы первой миграцией.
2. Подготовить сиды: один проект, 2 эксперимента, по 2 запуска, несколько capture session и датчиков; это позволит сразу тестировать эндпоинты и UI.
3. После синхронизации схемы обновить Pydantic-модели и OpenAPI (если потребуется `metadata`, `name` у run и т.д.).

Такой каркас покрывает все сущности блока Foundation и обеспечивает базу для последующих итераций (метрики, backfill, артефакты).

