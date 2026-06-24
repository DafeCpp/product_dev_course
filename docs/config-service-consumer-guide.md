# Config-Service Consumer Guide

Руководство для разработчиков backend-сервисов по интеграции с `config-service`.

---

## Архитектурный принцип

Config-service хранит runtime-конфигурацию платформы: feature flags, QoS-параметры, kill-switches. Значения изменяются оператором **без перезапуска** потребителей через `ConfigClient` SDK.

Приоритет источников конфигурации (по убыванию):
```
ENV-override > in-process cache > fallback file > default аргумент в get()
```

---

## Быстрый старт

### 1. Создать конфиг в config-service

```bash
curl -X POST http://localhost:8005/api/v1/config \
  -H "Content-Type: application/json" \
  -H "X-User-Id: admin" \
  -H "X-User-Is-Superadmin: true" \
  -d '{
    "service_name": "my-service",
    "key": "rate_limits",
    "config_type": "qos",
    "value": {"max_requests": 100, "window_seconds": 60},
    "description": "Per-sensor REST rate limits"
  }'
```

### 2. Добавить `ConfigClient` в сервис

```python
from backend_common.config_client import ConfigClient

client = ConfigClient(
    service_name="my-service",
    config_service_url="http://config-service:8005",
    poll_interval=5.0,          # секунды между опросами (с jitter ±25%)
    fallback_dir="/var/cache/config-service",  # резервная копия на диске
)
```

### 3. Подписаться на изменения ключа

```python
def on_config_update(configs: dict) -> None:
    raw = configs.get("rate_limits")
    if raw is None:
        return
    # валидируем и применяем
    ...

client.subscribe(on_config_update)
```

### 4. Запустить с aiohttp-приложением

```python
# в create_app()
app.on_startup.append(client.start)
app.on_cleanup.append(client.stop)
```

---

## ConfigClient API

### `get(key, *, default=None)`

Читает значение из кэша синхронно. Безопасно вызывать из hot-path.

```python
value = client.get("rate_limits", default={"max_requests": 0})
```

Порядок источников: `ENV-override → cache → default`.

### ENV-override

Любой ключ можно переопределить переменной окружения:

```
CONFIG_OVERRIDE__<SERVICE_NAME>__<KEY>=<json-or-string>
```

Пример:
```bash
CONFIG_OVERRIDE__MY_SERVICE__RATE_LIMITS='{"max_requests": 0}'
```

Переопределение работает немедленно и не требует перезапуска.

### `subscribe(callback)`

Callback вызывается после каждого успешного poll, если данные изменились (304 Not Modified пропускается). Поддерживает как sync, так и async функции.

```python
async def on_update(configs: dict) -> None:
    ...

client.subscribe(on_update)
```

---

## Паттерн QoS-конфига (рекомендуется)

По образцу `telemetry-ingest-service/workers/config_poller.py`:

```python
from pydantic import BaseModel
from typing import Optional

class _MyQosConfig(BaseModel):
    max_requests: Optional[int] = None
    window_seconds: Optional[float] = None
    model_config = {"extra": "ignore"}


# Shared mutable singleton — лимитер держит ссылку
_QOS_CONFIG = MyServiceQosConfig(max_requests=100, window_seconds=60.0)


def _apply_config(configs: dict) -> None:
    raw = configs.get("qos")
    if raw is None:
        return
    try:
        parsed = _MyQosConfig.model_validate(raw)
    except Exception:
        return  # fail-open: keep current values

    # Атомарная мутация in-place (asyncio single-threaded, await не нужен)
    if parsed.max_requests is not None:
        _QOS_CONFIG.max_requests = parsed.max_requests
    if parsed.window_seconds is not None:
        _QOS_CONFIG.window_seconds = parsed.window_seconds
```

**Важно:** не переприсваивайте глобал (`_QOS_CONFIG = new_config`). Лимитер держит ссылку на исходный объект. Используйте мутацию полей in-place.

---

## Fallback (cold-start без config-service)

При первом запуске, если config-service недоступен, `ConfigClient` читает файл с диска:

```
{fallback_dir}/{service_name}.json
```

Файл записывается автоматически после первого успешного poll (атомарно через tmp+rename).

При невалидном fallback-файле — предупреждение в лог, continue с дефолтами.

---

## Sensitive-значения

Конфиги с `is_sensitive: true` возвращаются в API как `"***"` для пользователей без прав `configs.sensitive.read` или superadmin.

`ConfigClient` (bulk-эндпоинт `/api/v1/configs/bulk`) получает **незаредактированные** значения — bulk работает только внутри Docker-сети и заблокирован на уровне auth-proxy (404).

---

## Roles & Permissions (RBAC)

Доступ к API config-service контролируется через fine-grained system-permissions,
которые auth-proxy инжектит в заголовок `X-User-System-Permissions` из эффективных
прав пользователя. Права собраны в 4 встроенные роли (auth-service миграция
`003_config_rbac.sql`).

### Роли

| Роль | Права |
|------|-------|
| `config_viewer` | `configs.view` |
| `config_editor` | `configs.view`, `configs.create`, `configs.update`, `configs.delete` |
| `config_operator` | `configs.view`, `configs.activate`, `configs.rollback` |
| `config_admin` | всё editor + operator + `configs.schemas.manage` + `configs.sensitive.read` |

**Принцип «4 глаз»:** editor создаёт/редактирует/удаляет черновики, но **не активирует**;
operator активирует/деактивирует/откатывает, но **не редактирует**. Это разделяет
авторство и публикацию (вторая пара глаз для прода). `superadmin` обходит все проверки.

### Матрица право → эндпоинт

| Право | Эндпоинты |
|-------|-----------|
| `configs.view` | `GET /config`, `GET /config/{id}`, `GET /config/{id}/history`, `GET /schemas`, `GET /schemas/{type}`, `GET /schemas/{type}/history` |
| `configs.create` | `POST /config` (вкл. `?dry_run=true`) |
| `configs.update` | `PATCH /config/{id}` (вкл. `?dry_run=true`) |
| `configs.delete` | `DELETE /config/{id}` |
| `configs.activate` | `POST /config/{id}/activate`, `POST /config/{id}/deactivate` |
| `configs.rollback` | `POST /config/{id}/rollback` |
| `configs.schemas.manage` | `PUT /schemas/{type}` |
| `configs.sensitive.read` | чтение незаредактированных `is_sensitive`-значений в GET/history |

Назначение ролей пользователям — через стандартный механизм auth-service (`roles.assign`).
В OpenAPI каждое требуемое право указано в расширении `x-required-permission`.

---

## Observability

### Prometheus-метрики (из `backend_common.config_client`)

| Метрика | Тип | Лейблы | Описание |
|---------|-----|--------|---------|
| `config_poll_total` | Counter | `service`, `status` (ok\|error\|not_modified) | Результаты опроса |
| `config_propagation_lag_seconds` | Histogram | `service` | Задержка от изменения в config-service до применения в потребителе |

### Алерты (в `infrastructure/monitoring/rules/alerts.yml`)

- `ConfigPropagationLagHigh` — p95 > 30s в течение 5 минут
- `ConfigBulkNoSuccessful200` — только 304 в течение 10 минут
- `ConfigSchemaViolationsHigh` — >0.1/s нарушений схемы
- `ConfigCompatRejectionHigh` — >0.05/s отклонений совместимости
- `ConfigIdempotencyConflicts` — конфликты idempotency key
- `ConfigOptimisticLockConflictsHigh` — >0.1/s конфликтов версий

---

## Типичные ошибки

### Конфиг не применяется после PUT

1. Проверь, что `ConfigClient` запущен (`start()` вызван)
2. Poll interval по умолчанию 5s; ускорь ENV: `CONFIG_CLIENT_POLL_INTERVAL_SECONDS=1`
3. Проверь метрику `config_poll_total{status="error"}` — возможно, сетевая ошибка

### 404 на `/api/v1/configs/bulk`

Ожидаемо извне сети — auth-proxy блокирует bulk (ADR-009). Убедись, что `ConfigClient` обращается напрямую к `config-service:8005`, не через `localhost:8080`.

### ConfigClient не стартует

Проверь переменные окружения:
```
CONFIG_CLIENT_ENABLED=true
CONFIG_CLIENT_URL=http://config-service:8005
CONFIG_CLIENT_POLL_INTERVAL_SECONDS=5.0
```

---

## Ссылки

- RFC-0001: `docs/RFC/rfc-0001-config-service.md`
- ADR-009: `docs/adr/ADR-009-auth-proxy-routing.md`
- Пример реализации poller: `projects/backend/services/telemetry-ingest-service/src/telemetry_ingest_service/workers/config_poller.py`
- OpenAPI spec: `projects/backend/services/config-service/openapi/openapi.yaml`
