-- 003_telemetry_dedup_index.sql
-- Add unique dedup index on telemetry_records to silently discard duplicate
-- readings that arrive during burst ingest (same sensor_id, timestamp, signal).
--
-- TimescaleDB requirement: every UNIQUE index on a hypertable MUST include
-- all partitioning columns. telemetry_records is partitioned by:
--   - time column:  timestamp   (required by TimescaleDB for all hypertables)
--   - space column: sensor_id   (required because number_partitions > 0)
-- Both are already present in the index below, so the constraint is satisfied.

BEGIN;

CREATE UNIQUE INDEX IF NOT EXISTS telemetry_records_dedup_idx
    ON telemetry_records (sensor_id, timestamp, signal);

COMMIT;
