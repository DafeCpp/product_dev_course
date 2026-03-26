# ADR 007: Comparison Service -- сравнение метрик нескольких runs

Статус: proposed
Дата: 2026-03-20

## Context

Пользователям нужно сравнивать метрики нескольких запусков (runs) одного эксперимента. KPI из ТЗ: TTM (time to metric comparison) < 10 секунд -- один API-вызов должен возвращать все данные для отрисовки сравнительного графика.

Текущее состояние:
- Таблица `run_metrics` существует: `(id, project_id, run_id, name, step, value, timestamp, created_at)`.
- Индекс `run_metrics_run_name_step_idx (run_id, name, step)` покрывает фильтрацию.
- Существующий API (`GET /runs/{run_id}/metrics`, `/summary`, `/aggregations`) работает с одним run.
- `RunRepository.get(project_id, run_id)` -- проверка существования и доступа.
- Нет batch-метода для получения данных нескольких runs одним запросом.

Проблема: для сравнения N runs через текущий API frontend должен сделать 2N запросов (summary + series для каждого). При 10 runs и 10 метриках это 20+ HTTP-запросов, latency не укладывается в 10 сек KPI.

## Decision

### 1. Архитектура: расширение MetricsService, а не отдельный сервис

Comparison -- stateless операция (вычисляется на лету, результат не сохраняется). Реализуется как новые методы в существующих слоях:
- `RunMetricsRepository` -- новый метод `fetch_comparison_data`
- `MetricsService` -- новый метод `compare_runs`
- Новый route-файл `api/routes/comparison.py`

Обоснование: данные уже в `run_metrics`, логика -- агрегация существующих запросов, отдельный микросервис не оправдан.

### 2. API Design

#### 2.1 POST /api/v1/experiments/{experiment_id}/compare

Основной endpoint. POST используется потому что тело запроса -- структурированный объект со списками (массив run_ids + массив metric_names), что плохо ложится на query parameters.

**Request:**
```json
{
  "run_ids": ["uuid-1", "uuid-2", "uuid-3"],
  "metric_names": ["loss", "accuracy"],
  "from_step": 0,
  "to_step": 1000,
  "max_points_per_series": 500
}
```

| Field | Type | Required | Constraints | Description |
|-------|------|----------|-------------|-------------|
| run_ids | uuid[] | yes | min 2, max 10 | Runs для сравнения |
| metric_names | string[] | yes | min 1, max 20 | Имена метрик |
| from_step | integer | no | >= 0 | Фильтр начала диапазона |
| to_step | integer | no | > from_step | Фильтр конца диапазона |
| max_points_per_series | integer | no | 100-2000, default 500 | Максимум точек на серию (для downsampling) |

**Response 200:**
```json
{
  "experiment_id": "uuid",
  "metric_names": ["loss", "accuracy"],
  "runs": [
    {
      "run_id": "uuid-1",
      "run_name": "baseline-v1",
      "status": "completed",
      "metrics": {
        "loss": {
          "summary": {
            "last_value": 0.01,
            "last_step": 1000,
            "min": 0.005,
            "max": 1.0,
            "avg": 0.2,
            "count": 1000
          },
          "series": [
            {"step": 0, "value": 1.0},
            {"step": 10, "value": 0.95}
          ]
        },
        "accuracy": {
          "summary": { "..." : "..." },
          "series": [...]
        }
      }
    }
  ]
}
```

#### 2.2 GET /api/v1/experiments/{experiment_id}/compare

Альтернативный endpoint для простых случаев (закладки, sharing URL).

**Query parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| run_ids | string | yes | Comma-separated UUIDs |
| names | string | yes | Comma-separated metric names |
| from_step | integer | no | Start step |
| to_step | integer | no | End step |
| max_points | integer | no | Max points per series, default 500 |

Ответ идентичен POST-варианту.

#### 2.3 RBAC

| Endpoint | Permission | Roles |
|----------|-----------|-------|
| POST .../compare | `experiments.view` | owner, editor, viewer |
| GET .../compare | `experiments.view` | owner, editor, viewer |

Comparison -- read-only операция, достаточно `experiments.view`.

#### 2.4 Валидация

- Все run_ids должны принадлежать указанному experiment_id. Проверяется одним SQL-запросом.
- Все run_ids должны существовать. Если хотя бы один не найден -- 404 с указанием отсутствующих.
- Все runs должны принадлежать одному project_id (берётся из X-Project-Id header).
- max 10 runs (ограничение предотвращает тяжёлые запросы).
- max 20 metric_names.

### 3. Repository Layer

#### 3.1 Новый метод: `fetch_runs_brief`

Получение метаданных нескольких runs одним запросом (для run_name, status в response).

```sql
SELECT id, name, status, experiment_id
FROM runs
WHERE project_id = $1
  AND experiment_id = $2
  AND id = ANY($3)
```

Размещение: `RunRepository` (расширяем существующий класс).

#### 3.2 Новый метод: `fetch_multi_run_summary`

Summary по нескольким runs одним запросом.

```sql
SELECT
    run_id,
    name,
    count(*)::bigint  AS total_steps,
    min(value)        AS min_value,
    max(value)        AS max_value,
    avg(value)        AS avg_value
FROM run_metrics
WHERE project_id = $1
  AND run_id = ANY($2)
  AND name = ANY($3)
GROUP BY run_id, name
ORDER BY run_id, name
```

Плюс last-value:
```sql
SELECT DISTINCT ON (run_id, name)
    run_id,
    name,
    step  AS last_step,
    value AS last_value
FROM run_metrics
WHERE project_id = $1
  AND run_id = ANY($2)
  AND name = ANY($3)
ORDER BY run_id, name, step DESC
```

Оба запроса используют индекс `run_metrics_run_name_step_idx` -- для каждого (run_id, name) это index scan.

#### 3.3 Новый метод: `fetch_multi_run_series`

Основной запрос для серий данных. Два варианта в зависимости от объёма данных.

**Вариант A: прямая выборка (когда точек <= max_points_per_series).**

```sql
SELECT run_id, name, step, value
FROM run_metrics
WHERE project_id = $1
  AND run_id = ANY($2)
  AND name = ANY($3)
  [AND step >= $N]
  [AND step <= $N]
ORDER BY run_id, name, step
```

**Вариант B: downsampling через bucketing (когда точек > max_points_per_series).**

Сервисный слой сначала определяет нужен ли downsampling (через count query), затем:

```sql
SELECT
    run_id,
    name,
    (step / $4) * $4 AS bucket_step,
    avg(value)        AS value
FROM run_metrics
WHERE project_id = $1
  AND run_id = ANY($2)
  AND name = ANY($3)
  [AND step >= $N]
  [AND step <= $N]
GROUP BY run_id, name, (step / $4)
ORDER BY run_id, name, bucket_step
```

`$4` = bucket_size, вычисляется как `max(1, total_steps / max_points_per_series)`.

#### 3.4 Новый метод: `count_multi_run_points`

Для определения необходимости downsampling:

```sql
SELECT run_id, name, count(*)::bigint AS cnt
FROM run_metrics
WHERE project_id = $1
  AND run_id = ANY($2)
  AND name = ANY($3)
  [AND step >= $N]
  [AND step <= $N]
GROUP BY run_id, name
```

### 4. Service Layer

Новый метод `MetricsService.compare_runs`:

```python
MAX_COMPARISON_RUNS = 10
MAX_COMPARISON_METRICS = 20
DEFAULT_MAX_POINTS = 500

async def compare_runs(
    self,
    project_id: UUID,
    experiment_id: UUID,
    *,
    run_ids: list[UUID],
    metric_names: list[str],
    from_step: int | None = None,
    to_step: int | None = None,
    max_points_per_series: int = DEFAULT_MAX_POINTS,
) -> dict:
    # 1. Validate constraints
    if len(run_ids) < 2 or len(run_ids) > MAX_COMPARISON_RUNS:
        raise ValueError(...)
    if len(metric_names) < 1 or len(metric_names) > MAX_COMPARISON_METRICS:
        raise ValueError(...)

    # 2. Fetch & validate runs (one query)
    runs = await self._run_repository.fetch_runs_brief(
        project_id, experiment_id, run_ids
    )
    found_ids = {r["id"] for r in runs}
    missing = set(run_ids) - found_ids
    if missing:
        raise NotFoundError(f"Runs not found: {missing}")

    # 3. Fetch summary (2 queries, parallelizable)
    agg_rows, last_rows = await asyncio.gather(
        self._metrics_repository.fetch_multi_run_summary(
            project_id, run_ids, metric_names
        ),
        self._metrics_repository.fetch_multi_run_last(
            project_id, run_ids, metric_names
        ),
    )

    # 4. Determine downsampling need (1 query)
    counts = await self._metrics_repository.count_multi_run_points(
        project_id, run_ids, metric_names,
        from_step=from_step, to_step=to_step,
    )

    # 5. Fetch series -- direct or bucketed per (run_id, name)
    #    Global bucket_size = max across all series
    max_count = max((c["cnt"] for c in counts), default=0)
    needs_downsampling = max_count > max_points_per_series

    if needs_downsampling:
        bucket_size = max(1, max_count // max_points_per_series)
        series_rows = await self._metrics_repository.fetch_multi_run_series_bucketed(
            project_id, run_ids, metric_names,
            bucket_size=bucket_size,
            from_step=from_step, to_step=to_step,
        )
    else:
        series_rows = await self._metrics_repository.fetch_multi_run_series(
            project_id, run_ids, metric_names,
            from_step=from_step, to_step=to_step,
        )

    # 6. Assemble response
    return self._build_comparison_response(
        experiment_id, runs, metric_names,
        agg_rows, last_rows, series_rows,
    )
```

Стратегия downsampling: единый `bucket_size` для всех серий в рамках одного comparison. Это гарантирует выровненные оси X на графике. Альтернатива (per-series bucket_size) усложняет визуализацию на фронтенде.

### 5. Индексы

Существующий индекс `run_metrics_run_name_step_idx (run_id, name, step)` покрывает все запросы comparison:
- `WHERE run_id = ANY($1)` раскладывается PostgreSQL в union of index scans для каждого run_id.
- GROUP BY run_id, name использует тот же индекс.

**Новый индекс не требуется.**

При масштабировании (>1M точек на run, >10 runs) -- рассмотреть составной индекс `(project_id, run_id, name, step)` или partial index. Но это отдельное решение, не для текущей итерации.

### 6. Оценка производительности (KPI: TTM < 10 сек)

Типичный сценарий: 5 runs x 2 метрики x 1000 steps = 10,000 строк.

| Операция | Запросов к БД | Ожидаемое время |
|----------|--------------|-----------------|
| fetch_runs_brief | 1 | < 5 ms |
| fetch_multi_run_summary | 1 | < 20 ms |
| fetch_multi_run_last | 1 | < 10 ms |
| count_multi_run_points | 1 | < 10 ms |
| fetch_multi_run_series | 1 | < 50 ms |
| Сериализация JSON | - | < 20 ms |
| HTTP overhead | - | < 10 ms |
| **Итого** | **5** | **< 125 ms** |

Worst case: 10 runs x 20 метрик x 100,000 steps = 20M строк. С downsampling (bucket_size ~200) получаем ~100K строк в ответе -- ожидаемое время < 3 сек. KPI выполняется.

Параллелизация summary + last через `asyncio.gather` сокращает время на ~10 ms.

### 7. OpenAPI Spec Changes

#### 7.1 Новый path в `openapi.yaml`

```yaml
/api/v1/experiments/{experiment_id}/compare:
  $ref: ./paths/experiments.yaml#/compare
```

#### 7.2 Новые операции в `paths/experiments.yaml`

```yaml
compare:
  get:
    tags: [metrics, experiments]
    summary: Compare metrics across runs (GET)
    operationId: compareRunMetricsGet
    parameters:
      - $ref: ../components/parameters.yaml#/ExperimentId
      - $ref: ../components/parameters.yaml#/ProjectIdQuery
      - name: run_ids
        in: query
        required: true
        schema:
          type: string
        description: Comma-separated run UUIDs (2-10)
      - name: names
        in: query
        required: true
        schema:
          type: string
        description: Comma-separated metric names (1-20)
      - name: from_step
        in: query
        schema:
          type: integer
      - name: to_step
        in: query
        schema:
          type: integer
      - name: max_points
        in: query
        schema:
          type: integer
          minimum: 100
          maximum: 2000
          default: 500
    responses:
      200:
        description: Comparison data
        content:
          application/json:
            schema:
              $ref: ../components/schemas.yaml#/ComparisonResponse
      400:
        description: Validation error (too many runs, missing params)
      404:
        description: Experiment or runs not found
  post:
    tags: [metrics, experiments]
    summary: Compare metrics across runs (POST)
    operationId: compareRunMetrics
    parameters:
      - $ref: ../components/parameters.yaml#/ExperimentId
      - $ref: ../components/parameters.yaml#/ProjectIdQuery
    requestBody:
      required: true
      content:
        application/json:
          schema:
            $ref: ../components/schemas.yaml#/ComparisonRequest
    responses:
      200:
        description: Comparison data
        content:
          application/json:
            schema:
              $ref: ../components/schemas.yaml#/ComparisonResponse
      400:
        description: Validation error
      404:
        description: Experiment or runs not found
```

#### 7.3 Новые schemas в `components/schemas.yaml`

```yaml
ComparisonRequest:
  type: object
  required:
    - run_ids
    - metric_names
  properties:
    run_ids:
      type: array
      items:
        type: string
        format: uuid
      minItems: 2
      maxItems: 10
    metric_names:
      type: array
      items:
        type: string
      minItems: 1
      maxItems: 20
    from_step:
      type: integer
      minimum: 0
    to_step:
      type: integer
    max_points_per_series:
      type: integer
      minimum: 100
      maximum: 2000
      default: 500

ComparisonResponse:
  type: object
  properties:
    experiment_id:
      type: string
      format: uuid
    metric_names:
      type: array
      items:
        type: string
    runs:
      type: array
      items:
        $ref: '#/ComparisonRunEntry'

ComparisonRunEntry:
  type: object
  properties:
    run_id:
      type: string
      format: uuid
    run_name:
      type: string
    status:
      $ref: '#/RunStatus'
    metrics:
      type: object
      additionalProperties:
        $ref: '#/ComparisonMetricData'

ComparisonMetricData:
  type: object
  properties:
    summary:
      $ref: '#/ComparisonMetricSummary'
    series:
      type: array
      items:
        $ref: '#/ComparisonPoint'

ComparisonMetricSummary:
  type: object
  properties:
    last_value:
      type: number
    last_step:
      type: integer
    min:
      type: number
    max:
      type: number
    avg:
      type: number
    count:
      type: integer

ComparisonPoint:
  type: object
  properties:
    step:
      type: integer
    value:
      type: number
```

### 8. Файлы для изменения/создания

#### Новые файлы:

| File | Purpose |
|------|---------|
| `api/routes/comparison.py` | Route handlers для POST/GET compare |

#### Модифицируемые файлы:

| File | Change |
|------|--------|
| `repositories/run_metrics.py` | Методы `fetch_multi_run_summary`, `fetch_multi_run_last`, `fetch_multi_run_series`, `fetch_multi_run_series_bucketed`, `count_multi_run_points` |
| `repositories/runs.py` | Метод `fetch_runs_brief(project_id, experiment_id, run_ids)` |
| `services/metrics.py` | Метод `compare_runs`, константы `MAX_COMPARISON_RUNS`, `MAX_COMPARISON_METRICS` |
| `services/dependencies.py` | Не требует изменений -- `get_metrics_service` уже инжектит оба repository |
| `openapi/openapi.yaml` | Новый path `/api/v1/experiments/{experiment_id}/compare` |
| `openapi/paths/experiments.yaml` | Секция `compare` с GET/POST |
| `openapi/components/schemas.yaml` | Schemas: `ComparisonRequest`, `ComparisonResponse`, `ComparisonRunEntry`, `ComparisonMetricData`, `ComparisonMetricSummary`, `ComparisonPoint` |
| `app.py` (или аналог регистрации routes) | Подключить `comparison.routes` |

#### Миграции БД: не требуются

### 9. Пограничные случаи

| Случай | Поведение |
|--------|-----------|
| Run не принадлежит experiment | 404 с перечислением невалидных run_ids |
| Метрика отсутствует для некоторых runs | В ответе для этого run metrics map не содержит ключа (пустой metrics block). Не ошибка -- runs могут логировать разные метрики |
| 0 точек для метрики (run ещё не начал логирование) | summary = null, series = [] |
| Runs из разных проектов | Невозможно -- project_id берётся из header, все runs фильтруются по нему |
| Дубликаты в run_ids | Дедупликация на уровне сервиса (set) |
| run_ids.length = 1 | 400 Bad Request -- сравнение требует минимум 2 runs |
| run_ids.length > 10 | 400 Bad Request |

### 10. Будущие расширения (не в текущем scope)

- **Saved comparisons**: сохранение набора (experiment_id, run_ids, metric_names) с permalink.
- **Diff mode**: вычисление разницы метрик между baseline run и остальными.
- **Cross-experiment comparison**: сравнение runs из разных экспериментов (требует другую валидацию).
- **WebSocket live comparison**: для runs в статусе `running` -- streaming обновлений.

## Consequences

**Positive:**
- Один API-вызов возвращает все данные для сравнительного графика. TTM < 10 сек гарантированно для типичных объёмов.
- Stateless дизайн -- нет новых таблиц, миграций, состояний для управления.
- Reuse существующих индексов и SQL-паттернов из ADR 006.
- GET-вариант endpoint позволяет bookmark/share URL сравнения.
- Downsampling с единым bucket_size упрощает фронтенд-визуализацию.

**Negative:**
- Тяжёлые запросы (10 runs x 20 метрик x 100K steps) могут создавать нагрузку на БД. Митигация: лимиты на runs/metrics/max_points, downsampling.
- POST для read-only операции -- нестандартно для REST. Митигация: GET-вариант для простых случаев, POST обоснован сложным телом запроса.
- Summary + series в одном ответе увеличивают размер payload. Митигация: max_points_per_series ограничивает серии.

**Risks:**
- При >10M строк в run_metrics `ANY($1)` с 10 UUID может привести к sequential scan вместо index scan. Митигация: `EXPLAIN ANALYZE` на staging, при необходимости переписать на UNION ALL.
- Большой JSON response (worst case ~5 MB) может быть медленным для сериализации. Митигация: max_points ограничение, в будущем -- streaming JSON.

## Alternatives Considered

1. **N параллельных вызовов существующего API с фронтенда.** Отклонено: не укладывается в KPI при >5 runs, лишний HTTP overhead, нет гарантии атомарности snapshot.

2. **GraphQL endpoint для flexible fetching.** Отклонено: проект использует REST, добавление GraphQL ради одного use case не оправдано.

3. **Materialized view для pre-computed comparison.** Отклонено: comparison -- ad-hoc операция с произвольными наборами runs, materialized view не подходит для динамических запросов.

4. **Сохранение comparison как сущности (ComparisonSession).** Отложено: можно добавить позже как надстройку поверх stateless API. Текущий дизайн не блокирует это расширение.
