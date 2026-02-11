-- 002_continuous_aggregates.sql
-- Add continuous aggregates for telemetry downsampling (1-minute buckets).
-- Requires TimescaleDB extension and hypertable (created in 001).

BEGIN;

-- 1-minute continuous aggregate: min/max/avg per sensor+signal+capture_session.
CREATE MATERIALIZED VIEW IF NOT EXISTS telemetry_1m
WITH (timescaledb.continuous) AS
SELECT
    time_bucket(INTERVAL '1 minute', "timestamp") AS bucket,
    sensor_id,
    signal,
    capture_session_id,
    count(*)                    AS sample_count,
    avg(raw_value)              AS avg_raw,
    min(raw_value)              AS min_raw,
    max(raw_value)              AS max_raw,
    avg(physical_value)         AS avg_physical,
    min(physical_value)         AS min_physical,
    max(physical_value)         AS max_physical
FROM telemetry_records
GROUP BY bucket, sensor_id, signal, capture_session_id
WITH NO DATA;

-- Refresh policy: keep aggregate up to date, look back 7 days for late arrivals,
-- leave 1-minute gap at the head (data still landing), run every 1 minute.
SELECT add_continuous_aggregate_policy(
    'telemetry_1m',
    start_offset    => INTERVAL '7 days',
    end_offset      => INTERVAL '1 minute',
    schedule_interval => INTERVAL '1 minute',
    if_not_exists   => TRUE
);

COMMIT;
