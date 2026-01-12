-- Minimal schema for telemetry-ingest-service integration tests.
BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;

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
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    FOREIGN KEY (run_id) REFERENCES runs (id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS telemetry_records (
    id bigserial PRIMARY KEY,
    project_id uuid NOT NULL,
    sensor_id uuid NOT NULL,
    run_id uuid,
    capture_session_id uuid,
    timestamp timestamptz NOT NULL,
    raw_value double precision NOT NULL,
    physical_value double precision,
    meta jsonb NOT NULL DEFAULT '{}'::jsonb,
    conversion_status telemetry_conversion_status NOT NULL DEFAULT 'raw_only',
    conversion_profile_id uuid,
    ingested_at timestamptz NOT NULL DEFAULT now(),
    FOREIGN KEY (sensor_id) REFERENCES sensors (id) ON DELETE CASCADE,
    FOREIGN KEY (run_id) REFERENCES runs (id) ON DELETE SET NULL,
    FOREIGN KEY (capture_session_id) REFERENCES capture_sessions (id) ON DELETE SET NULL
);

COMMIT;

