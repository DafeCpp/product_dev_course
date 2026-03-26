# ADR 006: Metrics Service -- API для работы с run_metrics

Статус: proposed
Дата: 2026-03-19

## Context

Таблица `run_metrics` существует в схеме experiment-service (миграция 001), но API реализован частично:
- `POST /api/v1/runs/{run_id}/metrics` -- есть (ingest batch), но без RBAC-проверок и idempotency.
- `GET /api/v1/runs/{run_id}/metrics` -- есть (query с фильтрами name, from_step, to_step), но без пагинации, лимитов и агрегаций.
- Агрегации min/avg/max по шагам -- отсутствуют (требование ТЗ 6.4).
- Summary (последнее значение каждой метрики) -- отсутствует.
- OpenAPI описывает только базовые ingest/query, без summary и aggregations.
- Frontend не имеет UI для метрик.

Существующие индексы:
```sql
CREATE INDEX run_metrics_run_name_step_idx ON run_metrics (run_id, name, step);
CREATE INDEX run_metrics_project_name_idx ON run_metrics (project_id, name);
```

## Decision

### 1. API Endpoints

#### 1.1 POST /api/v1/runs/{run_id}/metrics (доработка существующего)

Уже реализован. Изменения:
- Добавить RBAC: `ensure_permission(user, "metrics.write")` (editor/owner).
- Добавить Idempotency-Key (через существующий механизм IdempotencyService).
- Добавить лимит на размер батча: max 10 000 точек за запрос.
- Добавить webhook event `run.metrics_ingested`.

Request (без изменений):
```json
{
  "metrics": [
    {"name": "loss", "step": 1, "value": 0.95, "timestamp": "2026-03-19T10:00:00Z"},
    {"name": "loss", "step": 2, "value": 0.87, "timestamp": "2026-03-19T10:00:01Z"},
    {"name": "accuracy", "step": 1, "value": 0.12, "timestamp": "2026-03-19T10:00:00Z"}
  ]
}
```

Response 202 (без изменений):
```json
{"status": "accepted", "accepted": 3}
```

#### 1.2 GET /api/v1/runs/{run_id}/metrics (доработка существующего)

Изменения:
- Добавить RBAC: `ensure_permission(user, "experiments.view")` (viewer+).
- Добавить пагинацию: `limit` (default 1000, max 10000), `offset`.
- Добавить параметр `names` (comma-separated) для фильтрации по нескольким метрикам.
- Добавить `order` параметр: `asc` (default) / `desc` по step.

Query parameters:
| Параметр | Тип | Описание |
|----------|-----|----------|
| name | string | Фильтр по одному имени (обратная совместимость) |
| names | string | Comma-separated список имен метрик |
| from_step | integer | Начальный step (включительно) |
| to_step | integer | Конечный step (включительно) |
| limit | integer | Max записей, default 1000, max 10000 |
| offset | integer | Смещение, default 0 |
| order | string | `asc` (default) / `desc` по step |

Response 200:
```json
{
  "run_id": "uuid",
  "series": [
    {
      "name": "loss",
      "points": [
        {"step": 1, "value": 0.95, "timestamp": "2026-03-19T10:00:00Z"},
        {"step": 2, "value": 0.87, "timestamp": "2026-03-19T10:00:01Z"}
      ]
    }
  ],
  "total": 150,
  "limit": 1000,
  "offset": 0
}
```

#### 1.3 GET /api/v1/runs/{run_id}/metrics/summary (новый)

Возвращает последнее значение каждой метрики по максимальному step.

Query parameters:
| Параметр | Тип | Описание |
|----------|-----|----------|
| names | string | Comma-separated фильтр (опционально) |

Response 200:
```json
{
  "run_id": "uuid",
  "metrics": [
    {
      "name": "loss",
      "last_step": 100,
      "last_value": 0.023,
      "last_timestamp": "2026-03-19T10:05:00Z",
      "total_steps": 100,
      "min_value": 0.023,
      "max_value": 0.95,
      "avg_value": 0.342
    },
    {
      "name": "accuracy",
      "last_step": 100,
      "last_value": 0.978,
      "last_timestamp": "2026-03-19T10:05:00Z",
      "total_steps": 100,
      "min_value": 0.12,
      "max_value": 0.978,
      "avg_value": 0.654
    }
  ]
}
```

#### 1.4 GET /api/v1/runs/{run_id}/metrics/aggregations (новый)

Агрегации по шаговым окнам (step bucketing) для построения графиков с downsampling.

Query parameters:
| Параметр | Тип | Описание |
|----------|-----|----------|
| names | string | Comma-separated, обязательный |
| from_step | integer | Начальный step |
| to_step | integer | Конечный step |
| bucket_size | integer | Размер окна по step (default: auto-calculated) |
| agg | string | Comma-separated: `min`, `avg`, `max`, `count`, `last` (default: `min,avg,max`) |

Response 200:
```json
{
  "run_id": "uuid",
  "bucket_size": 10,
  "series": [
    {
      "name": "loss",
      "buckets": [
        {"step_from": 0, "step_to": 9, "min": 0.87, "avg": 0.91, "max": 0.95, "count": 10},
        {"step_from": 10, "step_to": 19, "min": 0.54, "avg": 0.67, "max": 0.86, "count": 10}
      ]
    }
  ]
}
```

Auto-bucket logic: если `bucket_size` не указан, выбирается так, чтобы количество bucket-ов было в диапазоне [100, 500]. Формула: `bucket_size = max(1, (to_step - from_step) / 300)`.

### 2. RBAC

| Endpoint | Требуемое permission | Роли |
|----------|---------------------|------|
| POST .../metrics | `metrics.write` | owner, editor |
| GET .../metrics | `experiments.view` | owner, editor, viewer |
| GET .../metrics/summary | `experiments.view` | owner, editor, viewer |
| GET .../metrics/aggregations | `experiments.view` | owner, editor, viewer |

Доступ к метрикам определяется через `project_id` запуска. Run проверяется через `RunRepository.get(project_id, run_id)` -- паттерн уже используется в существующем коде.

### 3. Repository Layer -- SQL Queries

#### 3.1 bulk_insert (существующий, без изменений)

```sql
INSERT INTO run_metrics (project_id, run_id, name, step, value, timestamp)
VALUES ($1, $2, $3, $4, $5, $6)
```

Используется `conn.executemany()`.

#### 3.2 fetch_series (доработка)

```sql
SELECT id, project_id, run_id, name, step, value, timestamp, created_at
FROM run_metrics
WHERE run_id = $1 AND project_id = $2
  [AND name = $3]
  [AND name = ANY($3)]   -- для names[]
  [AND step >= $N]
  [AND step <= $N]
ORDER BY name, step [ASC|DESC]
LIMIT $N OFFSET $N
```

Плюс count-запрос для total:
```sql
SELECT count(*) FROM run_metrics WHERE run_id = $1 AND project_id = $2 [... filters]
```

Использует индекс `run_metrics_run_name_step_idx`.

#### 3.3 fetch_summary (новый)

```sql
SELECT
    name,
    count(*)::bigint AS total_steps,
    min(value)       AS min_value,
    max(value)       AS max_value,
    avg(value)       AS avg_value
FROM run_metrics
WHERE run_id = $1 AND project_id = $2
  [AND name = ANY($3)]
GROUP BY name
ORDER BY name
```

Для `last_step` / `last_value` / `last_timestamp` -- отдельный запрос с DISTINCT ON:
```sql
SELECT DISTINCT ON (name)
    name, step AS last_step, value AS last_value, timestamp AS last_timestamp
FROM run_metrics
WHERE run_id = $1 AND project_id = $2
  [AND name = ANY($3)]
ORDER BY name, step DESC
```

Альтернатива -- один запрос с window functions, но два простых запроса проще для поддержки и используют index scan.

#### 3.4 fetch_aggregations (новый)

```sql
SELECT
    name,
    (step / $3) * $3          AS step_from,
    (step / $3) * $3 + $3 - 1 AS step_to,
    min(value)                 AS min_val,
    avg(value)                 AS avg_val,
    max(value)                 AS max_val,
    count(*)::bigint           AS cnt
FROM run_metrics
WHERE run_id = $1 AND project_id = $2
  AND name = ANY($4)
  [AND step >= $N]
  [AND step <= $N]
GROUP BY name, (step / $3)
ORDER BY name, step_from
```

Использует integer division для bucketing. Индекс `run_metrics_run_name_step_idx` покрывает этот запрос.

### 4. Service Layer

Расширить `MetricsService`:

```python
class MetricsService:
    BATCH_LIMIT = 10_000

    async def ingest_metrics(self, project_id, run_id, points) -> int:
        # Существующий + проверка len(points) <= BATCH_LIMIT

    async def query_metrics(self, project_id, run_id, *, name, names, from_step, to_step, limit, offset, order) -> dict:
        # Доработка: пагинация, names[], order

    async def get_summary(self, project_id, run_id, *, names: list[str] | None) -> dict:
        # Новый: вызывает repo.fetch_summary() + repo.fetch_last_per_metric()

    async def get_aggregations(self, project_id, run_id, *, names, from_step, to_step, bucket_size) -> dict:
        # Новый: auto-bucket, вызывает repo.fetch_aggregations()
```

### 5. Индексы

Существующих индексов достаточно для всех запросов:

| Индекс | Запросы |
|--------|---------|
| `run_metrics_run_name_step_idx (run_id, name, step)` | fetch_series, fetch_summary, fetch_aggregations, fetch_last_per_metric |
| `run_metrics_project_name_idx (project_id, name)` | cross-run queries (будущее) |

Новый индекс **не требуется**. Все запросы фильтруют по `run_id` + `name`, что покрывается первым индексом.

При необходимости (если появятся запросы по `(run_id, name, step DESC)`) -- добавить в отдельной миграции.

### 6. OpenAPI -- изменения

#### 6.1 Новые paths в openapi.yaml

```yaml
/api/v1/runs/{run_id}/metrics/summary:
  $ref: ./paths/runs.yaml#/metricsSummary
/api/v1/runs/{run_id}/metrics/aggregations:
  $ref: ./paths/runs.yaml#/metricsAggregations
```

#### 6.2 Новые операции в paths/runs.yaml

```yaml
metricsSummary:
  get:
    tags: [metrics]
    summary: Get metrics summary (last value, min/avg/max per metric)
    operationId: getMetricsSummary
    parameters:
      - $ref: ../components/parameters.yaml#/RunId
      - $ref: ../components/parameters.yaml#/ProjectIdQuery
      - name: names
        in: query
        schema:
          type: string
        description: Comma-separated metric names
    responses:
      200:
        description: Summary
        content:
          application/json:
            schema:
              $ref: ../components/schemas.yaml#/MetricSummaryResponse
      404:
        description: Run not found

metricsAggregations:
  get:
    tags: [metrics]
    summary: Get step-bucketed aggregations for selected metrics
    operationId: getMetricsAggregations
    parameters:
      - $ref: ../components/parameters.yaml#/RunId
      - $ref: ../components/parameters.yaml#/ProjectIdQuery
      - name: names
        in: query
        required: true
        schema:
          type: string
        description: Comma-separated metric names
      - name: from_step
        in: query
        schema:
          type: integer
      - name: to_step
        in: query
        schema:
          type: integer
      - name: bucket_size
        in: query
        schema:
          type: integer
          minimum: 1
      - name: agg
        in: query
        schema:
          type: string
          default: min,avg,max
    responses:
      200:
        description: Aggregated metrics
        content:
          application/json:
            schema:
              $ref: ../components/schemas.yaml#/MetricAggregationsResponse
      404:
        description: Run not found
```

#### 6.3 Новые schemas в components/schemas.yaml

```yaml
MetricSummaryResponse:
  type: object
  properties:
    run_id:
      type: string
      format: uuid
    metrics:
      type: array
      items:
        $ref: '#/MetricSummaryItem'

MetricSummaryItem:
  type: object
  properties:
    name:
      type: string
    last_step:
      type: integer
    last_value:
      type: number
    last_timestamp:
      type: string
      format: date-time
    total_steps:
      type: integer
    min_value:
      type: number
    max_value:
      type: number
    avg_value:
      type: number

MetricAggregationsResponse:
  type: object
  properties:
    run_id:
      type: string
      format: uuid
    bucket_size:
      type: integer
    series:
      type: array
      items:
        type: object
        properties:
          name:
            type: string
          buckets:
            type: array
            items:
              $ref: '#/MetricBucket'

MetricBucket:
  type: object
  properties:
    step_from:
      type: integer
    step_to:
      type: integer
    min:
      type: number
    avg:
      type: number
    max:
      type: number
    count:
      type: integer
```

#### 6.4 Обновление MetricQueryResponse

Добавить поля пагинации:
```yaml
MetricQueryResponse:
  type: object
  properties:
    run_id:
      type: string
      format: uuid
    series:
      ...existing...
    total:
      type: integer
    limit:
      type: integer
    offset:
      type: integer
```

### 7. Файлы для изменения/создания

#### Модифицируемые файлы:

| Файл | Изменение |
|------|-----------|
| `api/routes/metrics.py` | Добавить RBAC, endpoints summary и aggregations, пагинацию в query |
| `services/metrics.py` | Добавить методы get_summary, get_aggregations, batch limit, пагинацию |
| `repositories/run_metrics.py` | Добавить методы fetch_summary, fetch_last_per_metric, fetch_aggregations, count, доработать fetch_series |
| `openapi/openapi.yaml` | Добавить paths для summary и aggregations |
| `openapi/paths/runs.yaml` | Добавить metricsSummary и metricsAggregations |
| `openapi/components/schemas.yaml` | Добавить MetricSummaryResponse, MetricSummaryItem, MetricAggregationsResponse, MetricBucket; обновить MetricQueryResponse |

#### Новых файлов не требуется

Вся логика добавляется в существующие модули metrics (route, service, repository).

### 8. Frontend

#### 8.1 Типы (генерируются из OpenAPI через `make generate-sdk`)

Ключевые сгенерированные типы:
- `MetricSummaryResponse`, `MetricSummaryItem`
- `MetricAggregationsResponse`, `MetricBucket`
- Обновленный `MetricQueryResponse` с пагинацией

#### 8.2 API Client

Добавить в сгенерированный SDK (или ручной api-client):
- `ingestMetrics(runId, metrics, opts?)` -- POST
- `queryMetrics(runId, params?)` -- GET
- `getMetricsSummary(runId, params?)` -- GET
- `getMetricsAggregations(runId, params)` -- GET

#### 8.3 UI-компоненты

| Компонент | Расположение | Описание |
|-----------|-------------|----------|
| `MetricsSummaryTable` | Run detail page, tab "Metrics" | Таблица с последним значением, min/avg/max для каждой метрики. Источник: GET /metrics/summary |
| `MetricsChart` | Run detail page, tab "Metrics" | Plotly `scattergl` график step vs value. Поддержка нескольких метрик на одном графике. Для больших серий (>5000 точек) использует GET /metrics/aggregations |
| `MetricSelector` | Sidebar или dropdown в MetricsChart | Checkboxes для выбора метрик из доступных (список из summary) |
| `MetricsCompareChart` | Будущее (Comparison Service) | Наложение метрик из разных runs |

Порядок реализации frontend: MetricsSummaryTable -> MetricsChart -> MetricSelector.

### 9. Webhook-событие

Добавить event type `run.metrics_ingested`:
```json
{
  "run_id": "uuid",
  "experiment_id": "uuid",
  "metric_names": ["loss", "accuracy"],
  "points_count": 3
}
```

Это позволит внешним системам реагировать на поступление новых метрик (например, автоматическая визуализация, алерты по порогам).

## Consequences

**Positive:**
- Закрывает требование ТЗ 6.4 (endpoint для метрик + агрегации).
- Использует существующую таблицу и индексы -- миграция БД не нужна.
- Вписывается в существующую архитектуру (routes -> services -> repositories).
- Aggregations endpoint позволяет frontend эффективно отображать большие серии.
- Summary endpoint дает быстрый обзор метрик запуска без загрузки всех точек.

**Negative:**
- Aggregations по step (а не по time) -- специфичное решение. Если потребуется time-based downsampling, нужен отдельный endpoint или параметр.
- Два SQL-запроса для summary (аггрегации + DISTINCT ON для last value) вместо одного -- компромисс ради простоты.

**Risks:**
- При больших сериях (>1M точек на один run) fetch_aggregations может стать медленным. Митигация: limit 10000 на ingest, bucket_size auto-calculation, LIMIT на bucket count.
- `SELECT *` в fetch_series заменить на явный список колонок для forward compatibility.

## Альтернативы рассмотренные

1. **Отдельный metrics-service (микросервис).** Отклонено: таблица уже в БД experiment-service, объем кода небольшой, нет причин для отдельного процесса.
2. **TimescaleDB hypertable для run_metrics.** Отклонено: run_metrics -- не time-series в классическом смысле (ключ -- step, не время). Обычные индексы PostgreSQL достаточны. Пересмотреть если объемы вырастут >10M строк.
3. **Materialized view для summary.** Отклонено: summary рассчитывается на лету по индексу за миллисекунды при типичных объемах (<100K точек на run). Materialized view усложняет схему и требует refresh.
