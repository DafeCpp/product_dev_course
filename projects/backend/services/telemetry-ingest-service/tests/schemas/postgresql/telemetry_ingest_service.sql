-- Minimal schema for telemetry-ingest-service integration tests.
BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;
CREATE EXTENSION IF NOT EXISTS timescaledb;

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'sensor_status') THEN
        CREATE TYPE sensor_status AS ENUM ('registering', 'active', 'inactive', 'decommissioned');
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'telemetry_conversion_status') THEN
        CREATE TYPE telemetry_conversion_status AS ENUM ('raw_only', 'converted', 'client_provided', 'conversion_failed');
    END IF;
END$$;

CREATE TABLE IF NOT EXISTS sensors (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    project_id uuid NOT NULL,
    name text NOT NULL DEFAULT 'test',
    type text NOT NULL DEFAULT 'test',
    input_unit text,
    display_unit text,
    status sensor_status NOT NULL DEFAULT 'registering',
    token_hash bytea,
    token_preview text,
    active_profile_id uuid,
    calibration_notes text,
    last_heartbeat timestamptz,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS runs (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    project_id uuid NOT NULL,
    experiment_id uuid,
    created_by uuid,
    name text,
    status text,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS capture_sessions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    run_id uuid NOT NULL,
    project_id uuid NOT NULL,
    ordinal_number integer NOT NULL DEFAULT 1,
    status text NOT NULL DEFAULT 'draft',
    archived boolean NOT NULL DEFAULT false,
    started_at timestamptz,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    FOREIGN KEY (run_id) REFERENCES runs (id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS run_sensors (
    run_id uuid NOT NULL,
    sensor_id uuid NOT NULL,
    project_id uuid NOT NULL,
    detached_at timestamptz,
    PRIMARY KEY (run_id, sensor_id),
    FOREIGN KEY (run_id) REFERENCES runs (id) ON DELETE CASCADE,
    FOREIGN KEY (sensor_id) REFERENCES sensors (id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS telemetry_records (
    id bigserial NOT NULL,
    project_id uuid NOT NULL,
    sensor_id uuid NOT NULL,
    run_id uuid,
    capture_session_id uuid,
    timestamp timestamptz NOT NULL,
    raw_value double precision NOT NULL,
    physical_value double precision,
    meta jsonb NOT NULL DEFAULT '{}'::jsonb,
    signal text GENERATED ALWAYS AS ((meta->>'signal')) STORED,
    conversion_status telemetry_conversion_status NOT NULL DEFAULT 'raw_only',
    conversion_profile_id uuid,
    ingested_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (sensor_id, timestamp, id),
    FOREIGN KEY (sensor_id) REFERENCES sensors (id) ON DELETE CASCADE,
    FOREIGN KEY (run_id) REFERENCES runs (id) ON DELETE SET NULL,
    FOREIGN KEY (capture_session_id) REFERENCES capture_sessions (id) ON DELETE SET NULL
);

SELECT create_hypertable(
    'telemetry_records',
    'timestamp',
    partitioning_column => 'sensor_id',
    number_partitions => 2,
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

CREATE INDEX IF NOT EXISTS telemetry_records_project_sensor_ts_id_idx
    ON telemetry_records (project_id, sensor_id, timestamp ASC, id ASC);

COMMIT;

