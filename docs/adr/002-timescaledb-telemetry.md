# ADR 002: TimescaleDB для хранения телеметрии датчиков

Статус: accepted
Дата: 2026-01-13
Реализация: ✅ выполнена (см. `docs/experiment-tracking-status-and-roadmap.md`)

## Контекст

В системе есть поток телеметрии от датчиков (в т.ч. embedded/RC), который доставляется в Experiment Service через ingest (`POST /api/v1/telemetry`) и сохраняется в таблицу `telemetry_records`.

Характер нагрузки:
- частые вставки (time-series);
- запросы по диапазонам времени (графики/аналитика/диагностика);
- необходимость долгого хранения при приемлемой стоимости (retention/compression);
- потребность в downsampling/агрегациях по времени.

## Решение

Использовать **TimescaleDB** (расширение PostgreSQL) как целевую технологию хранения телеметрии:
- телеметрия хранится **в той же базе**, что и данные экспериментов (вариант A), с изоляцией на уровне схемы/пулов подключений;
- `telemetry_records` переводится в hypertable по `timestamp` (опционально с partitioning по `sensor_id`);
- вводится индексируемое поле `signal` (рекомендуемо — generated column из `meta->>'signal'`);
- для старых данных включаются политики **compression** и **retention**;
- для дешёвых графиков вводятся **continuous aggregates** (например, 1m/10s).

Подробные практические рекомендации зафиксированы в `docs/telemetry-storage-timescaledb.md`.

## Последствия

Плюсы:
- сохраняем PostgreSQL-совместимость (SQL/JOIN/транзакции/индексы/экосистема);
- проще масштабировать time-series нагрузку (hypertable, chunking);
- удобные штатные механизмы retention/compression;
- continuous aggregates закрывают типовые задачи downsampling.

Минусы/риски:
- повышается операционная сложность (extension, апгрейды, бэкапы/restore, параметры TimescaleDB);
- на малых объёмах «обычный» Postgres может быть проще и достаточно быстрым;
- нужно учитывать политику лицензирования/использования TimescaleDB для выбранного способа деплоя.

## Альтернативы

- PostgreSQL без TimescaleDB:
  - индексы + (опционально) native partitioning по времени;
  - проще в эксплуатации, но больше ручной работы/ограничений под нагрузкой.
- Колонночные/TSDB (ClickHouse/InfluxDB/Prometheus remote-write):
  - потенциально выше эффективность под метрики, но сильнее меняется стек и интеграции.

## План внедрения (черновой)

- В dev/stage включить TimescaleDB и перевести `telemetry_records` в hypertable.
- Добавить/зафиксировать политики chunking/compression/retention на основе реальных цифр ingestion.
- Ввести continuous aggregates для UI/дашбордов.

