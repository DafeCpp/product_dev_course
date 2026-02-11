# Хранение телеметрии датчиков в TimescaleDB

Этот документ фиксирует рекомендации по хранению телеметрии датчиков (time-series) в Experiment Service на базе PostgreSQL + TimescaleDB.

Статус: **реализовано** (см. roadmap Experiment Service).

Контекст:
- ingest принимает батчи readings через `POST /api/v1/telemetry` (см. `docs/telemetry-cli-ts.md`, `docs/telemetry-rc-stm32.md`);
- текущая БД хранит точки в таблице `telemetry_records` (см. миграции `projects/backend/services/experiment-service/migrations/001_initial_schema.sql`).

## Когда TimescaleDB оправдан

TimescaleDB стоит включать, когда появляется сочетание факторов:
- высокая частота записей (много точек/сек на датчик и/или много датчиков),
- частые запросы «за диапазон времени» (графики, выборка последних N минут/часов/дней),
- потребность в downsampling (агрегации по времени) и долгом хранении с retention/compression.

Если объёмы небольшие, можно оставаться на «обычном» PostgreSQL с индексами на `(sensor_id, timestamp)` и вернуться к TimescaleDB позже.

## Базовая модель данных (как есть)

Точки телеметрии хранятся в `telemetry_records` и содержат:
- `timestamp` — время измерения;
- `sensor_id`, опционально `run_id`/`capture_session_id`;
- `raw_value`, опционально `physical_value`;
- `meta` (JSONB), где обычно лежит `signal` (см. `docs/telemetry-rc-stm32.md`).

## Размещение в инфраструктуре (выбранный вариант A)

Выбранный подход: **хранить телеметрию в той же базе PostgreSQL/TimescaleDB, что и данные экспериментов**.

Причины:
- проще связывать телеметрию с доменными сущностями (`run_id`, `capture_session_id`, `sensor_id`) без межбазовой интеграции;
- проще в эксплуатации (один инстанс/кластер, один набор бэкапов/миграций);
- можно включать TimescaleDB локально/постепенно и не усложнять MVP.

Как снизить риски влияния ingestion на CRUD:
- выделить телеметрию логически (минимум — отдельные таблицы; по желанию — отдельная schema `telemetry`);
- использовать отдельные connection pools/лимиты для ingest и для API CRUD;
- включить hypertable + корректные индексы + retention/compression, чтобы не раздувать вакуум/индексы бесконечно.

## Рекомендуемая конфигурация TimescaleDB

### 1) Включить расширение

```sql
CREATE EXTENSION IF NOT EXISTS timescaledb;
```

### 2) Сделать `telemetry_records` hypertable

Рекомендованный старт:
- time-column: `timestamp`
- space partitioning: `sensor_id` (если датчиков много и ingestion параллельный)
- chunk interval: обычно 1 день (под нагрузку подбирается отдельно)

```sql
SELECT create_hypertable(
  'telemetry_records',
  by_range('timestamp'),
  by_hash('sensor_id', 8),
  chunk_time_interval => INTERVAL '1 day',
  if_not_exists => TRUE
);
```

Примечания:
- число партиций по `sensor_id` (здесь `8`) выбирается по числу CPU/конкурентности ingest;
- `chunk_time_interval` выбирается по объёму данных (цель: «разумный» размер чанка, не слишком мелко и не слишком крупно).

### 3) Вынести `signal` в индексируемое поле (опционально, но очень желательно)

Поскольку `signal` часто используется в фильтрах/графиках, хранить его только в `meta` неудобно. Рекомендуемый компромисс — добавить generated column:

```sql
ALTER TABLE telemetry_records
  ADD COLUMN signal text
  GENERATED ALWAYS AS ((meta->>'signal')) STORED;
```

И добавить индекс под самый частый паттерн «конкретный датчик + конкретный сигнал + диапазон времени»:

```sql
CREATE INDEX IF NOT EXISTS telemetry_records_sensor_signal_ts_idx
  ON telemetry_records (sensor_id, signal, timestamp DESC);
```

### 4) Включить сжатие (compression) для «старых» чанков

Рекомендованный базовый подход:
- сжимать чанки старше N дней;
- сегментировать по `sensor_id` и `signal`;
- сортировать по `timestamp` (DESC).

```sql
ALTER TABLE telemetry_records
  SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'sensor_id, signal',
    timescaledb.compress_orderby = 'timestamp DESC'
  );

SELECT add_compression_policy('telemetry_records', INTERVAL '7 days');
```

### 5) Retention для «сырых» точек

Обычно выгодно хранить «сырьё» ограниченное время, а долгий горизонт обеспечивать агрегатами:

```sql
SELECT add_retention_policy('telemetry_records', INTERVAL '90 days');
```

## Continuous aggregates (downsampling)

Для дешёвых графиков/дашбордов обычно нужен downsampling (например, 1s/10s/1m). Это можно сделать continuous aggregate, например для 1 минутной агрегации:

```sql
CREATE MATERIALIZED VIEW telemetry_1m
WITH (timescaledb.continuous) AS
SELECT
  time_bucket(INTERVAL '1 minute', timestamp) AS bucket,
  sensor_id,
  signal,
  count(*) AS n,
  avg(raw_value) AS avg_raw,
  min(raw_value) AS min_raw,
  max(raw_value) AS max_raw
FROM telemetry_records
GROUP BY bucket, sensor_id, signal;
```

Политика обновления (параметры подбираются под ingestion/задержки):

```sql
SELECT add_continuous_aggregate_policy(
  'telemetry_1m',
  start_offset => INTERVAL '7 days',
  end_offset => INTERVAL '1 minute',
  schedule_interval => INTERVAL '1 minute'
);
```

## Рекомендации по ingestion (чтобы не упереться в БД)

- Отправлять readings батчами (у вас это уже заложено в `telemetry-cli`).
- Вставлять батчами (multi-row insert / COPY), избегать «1 HTTP request → 1 INSERT → 1 reading».
- Для реального железа учитывать повторы/перепосылки: если нужна идемпотентность на уровне точек — обсуждать ключ уникальности (например, `(sensor_id, signal, timestamp, seq)`), либо принимать best-effort (текущий MVP).

## Открытые вопросы (требуют чисел)

Чтобы точно выбрать `chunk_time_interval`, число партиций и retention/compression, нужны:
- количество датчиков,
- частота (Гц) и число сигналов,
- сроки хранения «сырья» vs агрегатов,
- основные запросы (по одному датчику, по группе датчиков, алерты по порогам и т.п.).

