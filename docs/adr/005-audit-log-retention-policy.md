# ADR 004: Audit Log Retention Policy

Статус: proposed
Дата: 2026-03-19

## Контекст

ТЗ требует хранение аудит-лога минимум 1 год (365 дней). Сейчас retention не настроен -- данные растут бесконечно.

Аудит-лог хранится в трех таблицах двух сервисов:

| Сервис | Таблица | PK | Индекс по времени | FK |
|--------|---------|----|--------------------|-----|
| auth-service | `audit_log` | uuid | `audit_log_timestamp_idx (timestamp DESC)` | нет (actor_id без FK) |
| experiment-service | `run_events` | bigserial | `run_events_run_idx (run_id, created_at DESC)` | `run_id -> runs(id) ON DELETE CASCADE` |
| experiment-service | `capture_session_events` | bigserial | `capture_session_events_session_idx (session_id, created_at DESC)` | `capture_session_id -> capture_sessions(id) ON DELETE CASCADE` |

Объемы: при нормальной нагрузке десятки тысяч записей в год (не миллионы). Это не TSDB-scale данные.

## Решение

### 1. Конфигурация

Добавить `AUDIT_RETENTION_DAYS` в оба сервиса. Значение по умолчанию -- 365.

**auth-service `settings.py`:**
```python
audit_retention_days: int = 365
```

**experiment-service `settings.py`:**
```python
audit_retention_days: int = 365
```

### 2. Background worker -- batch DELETE с ограничением

#### 2.1 Experiment-service

Новый файл: `workers/audit_retention_cleanup.py`

```python
"""Worker: purge audit events older than retention period."""
from __future__ import annotations

from datetime import datetime, timedelta

from backend_common.db.pool import get_pool_service as get_pool

from experiment_service.settings import settings

_BATCH_SIZE = 1000


async def audit_retention_cleanup(now: datetime) -> str | None:
    """Delete run_events and capture_session_events older than retention."""
    if settings.audit_retention_days <= 0:
        return None

    pool = await get_pool()
    cutoff = now - timedelta(days=settings.audit_retention_days)
    total_deleted = 0

    async with pool.acquire() as conn:
        # run_events: batch delete by created_at
        while True:
            result = await conn.execute(
                """
                DELETE FROM run_events
                WHERE id IN (
                    SELECT id FROM run_events
                    WHERE created_at < $1
                    ORDER BY id
                    LIMIT $2
                )
                """,
                cutoff,
                _BATCH_SIZE,
            )
            deleted = int(result.split()[-1])
            total_deleted += deleted
            if deleted < _BATCH_SIZE:
                break

        # capture_session_events: batch delete by created_at
        while True:
            result = await conn.execute(
                """
                DELETE FROM capture_session_events
                WHERE id IN (
                    SELECT id FROM capture_session_events
                    WHERE created_at < $1
                    ORDER BY id
                    LIMIT $2
                )
                """,
                cutoff,
                _BATCH_SIZE,
            )
            deleted = int(result.split()[-1])
            total_deleted += deleted
            if deleted < _BATCH_SIZE:
                break

    return f"audit_purged={total_deleted}" if total_deleted else None
```

Зарегистрировать в `workers/__init__.py`:
```python
WorkerTask(name="audit_retention_cleanup", fn=audit_retention_cleanup),
```

#### 2.2 Auth-service

Auth-service сейчас не имеет BackgroundWorker. Два варианта:

**Вариант A (рекомендуемый):** Добавить BackgroundWorker из backend_common в auth-service по аналогии с experiment-service. Минимальная обвязка: один worker с одной задачей.

**Вариант B:** Отдельный cron-job / SQL-скрипт. Проще, но выпадает из единообразной архитектуры.

Выбираем **Вариант A**. Создать:

- `auth_service/workers/__init__.py` -- по аналогии с experiment-service
- `auth_service/workers/audit_retention_cleanup.py`:

```python
"""Worker: purge audit_log entries older than retention period."""
from __future__ import annotations

from datetime import datetime, timedelta

from backend_common.db.pool import get_pool_service as get_pool

from auth_service.settings import settings

_BATCH_SIZE = 1000


async def audit_retention_cleanup(now: datetime) -> str | None:
    """Delete audit_log entries older than retention."""
    if settings.audit_retention_days <= 0:
        return None

    pool = await get_pool()
    cutoff = now - timedelta(days=settings.audit_retention_days)
    total_deleted = 0

    async with pool.acquire() as conn:
        while True:
            result = await conn.execute(
                """
                DELETE FROM audit_log
                WHERE id IN (
                    SELECT id FROM audit_log
                    WHERE timestamp < $1
                    ORDER BY timestamp
                    LIMIT $2
                )
                """,
                cutoff,
                _BATCH_SIZE,
            )
            deleted = int(result.split()[-1])
            total_deleted += deleted
            if deleted < _BATCH_SIZE:
                break

    return f"audit_purged={total_deleted}" if total_deleted else None
```

- `auth_service/workers/revoked_tokens_cleanup.py` -- бонус: очистка истекших revoked_tokens (сейчас не делается, а `revoked_tokens_expires_at_idx` уже есть)

Подключить в `auth_service/main.py`:
```python
from auth_service.workers import start_background_worker, stop_background_worker
# ...
app.on_startup.append(start_background_worker)
app.on_cleanup.append(stop_background_worker)
```

### 3. Партиционирование -- НЕ нужно

Обоснование:
- **Объем данных:** аудит-лог -- единицы-десятки тысяч записей в год. Это не телеметрия.
- **Batch DELETE с LIMIT** достаточно эффективен при наличии индекса по `timestamp`/`created_at`.
- `audit_log` уже имеет `audit_log_timestamp_idx (timestamp DESC)` -- subquery в DELETE будет использовать index scan.
- `run_events` и `capture_session_events` не имеют отдельного индекса по `created_at` -- нужна миграция (см. ниже).
- Партиционирование усложняет схему (FK не работают через партиции, нужен partition management), а выигрыш при таких объемах -- нулевой.
- Если объем вырастет до миллионов записей в год -- вернуться к партиционированию (отдельный ADR).

### 4. Миграция: индексы для эффективного cleanup

Новая миграция `002_audit_retention_indexes.sql` в experiment-service:

```sql
BEGIN;

CREATE INDEX CONCURRENTLY IF NOT EXISTS run_events_created_at_idx
    ON run_events (created_at);

CREATE INDEX CONCURRENTLY IF NOT EXISTS capture_session_events_created_at_idx
    ON capture_session_events (created_at);

COMMIT;
```

Auth-service: индекс `audit_log_timestamp_idx` уже существует -- миграция не нужна.

### 5. Переменные окружения

Добавить в `.env.example` и `.env.docker.example`:

```
# Audit log retention (days). Minimum 365 per compliance requirements.
AUDIT_RETENTION_DAYS=365
```

## Consequences

**Positive:**
- Выполнение требования ТЗ: retention минимум 365 дней.
- Единообразная архитектура: оба сервиса используют BackgroundWorker из backend_common.
- Batch delete не блокирует таблицу надолго (батчи по 1000).
- Настраиваемый retention через env var.

**Negative:**
- Auth-service получает новую зависимость (BackgroundWorker), хотя раньше был stateless в плане фоновых задач.
- При большом accumulated backlog первый cleanup может занять время (но батчи это смягчают).

**Risks:**
- `DELETE ... WHERE id IN (SELECT ... LIMIT N)` на PostgreSQL 16 эффективен, но при очень больших backlog-ах (миллионы строк) может создать нагрузку. Митигация: batch size = 1000, worker interval = 60 сек.
- FK CASCADE на `run_events.run_id` означает, что при удалении run записи events удалятся автоматически. Retention cleanup -- дополнительная очистка для случаев, когда run жив, а events устарели.

## Затронутые файлы

### Новые файлы:
1. `projects/backend/services/auth-service/src/auth_service/workers/__init__.py`
2. `projects/backend/services/auth-service/src/auth_service/workers/audit_retention_cleanup.py`
3. `projects/backend/services/auth-service/src/auth_service/workers/revoked_tokens_cleanup.py`
4. `projects/backend/services/experiment-service/src/experiment_service/workers/audit_retention_cleanup.py`
5. `projects/backend/services/experiment-service/migrations/002_audit_retention_indexes.sql`

### Модифицируемые файлы:
6. `projects/backend/services/auth-service/src/auth_service/settings.py` -- +`audit_retention_days`, +`worker_interval_seconds`
7. `projects/backend/services/auth-service/src/auth_service/main.py` -- подключить BackgroundWorker
8. `projects/backend/services/experiment-service/src/experiment_service/settings.py` -- +`audit_retention_days`
9. `projects/backend/services/experiment-service/src/experiment_service/workers/__init__.py` -- +audit_retention_cleanup task
10. `.env.example`, `.env.docker.example` -- +AUDIT_RETENTION_DAYS
