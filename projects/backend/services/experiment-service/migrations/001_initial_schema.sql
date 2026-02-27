-- 001_initial_schema.sql
-- Initial Experiment Service schema (single init; includes TimescaleDB telemetry + later additive migrations).

-- pgcrypto и timescaledb создаются при создании БД (Terraform / init script), не миграцией.
BEGIN;

CREATE OR REPLACE FUNCTION set_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = now();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TYPE experiment_status AS ENUM ('draft', 'running', 'failed', 'succeeded', 'archived');
CREATE TYPE run_status AS ENUM ('draft', 'running', 'failed', 'succeeded', 'archived');
CREATE TYPE capture_session_status AS ENUM ('draft', 'running', 'failed', 'succeeded', 'archived', 'backfilling');
CREATE TYPE sensor_status AS ENUM ('registering', 'active', 'inactive', 'decommissioned');
CREATE TYPE conversion_profile_status AS ENUM ('draft', 'active', 'scheduled', 'deprecated');
CREATE TYPE telemetry_conversion_status AS ENUM ('raw_only', 'converted', 'client_provided', 'conversion_failed');

CREATE TABLE experiments (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    project_id uuid NOT NULL,
    owner_id uuid NOT NULL,
    name text NOT NULL,
    description text,
    experiment_type text,
    tags text[] NOT NULL DEFAULT '{}'::text[],
    metadata jsonb NOT NULL DEFAULT '{}'::jsonb,
    status experiment_status NOT NULL DEFAULT 'draft',
    archived_at timestamptz,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (id, project_id)
);

CREATE UNIQUE INDEX experiments_project_name_uindex ON experiments (project_id, lower(name));
CREATE INDEX experiments_project_status_idx ON experiments (project_id, status);
CREATE INDEX experiments_tags_gin_idx ON experiments USING gin (tags);
CREATE INDEX experiments_metadata_gin_idx ON experiments USING gin (metadata);

CREATE TRIGGER experiments_set_updated_at
    BEFORE UPDATE ON experiments
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

CREATE TABLE runs (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    experiment_id uuid NOT NULL,
    project_id uuid NOT NULL,
    created_by uuid NOT NULL,
    name text,
    params jsonb NOT NULL DEFAULT '{}'::jsonb,
    git_sha text,
    env text,
    tags text[] NOT NULL DEFAULT '{}'::text[],
    status run_status NOT NULL DEFAULT 'draft',
    started_at timestamptz,
    finished_at timestamptz,
    duration_seconds integer,
    notes text,
    metadata jsonb NOT NULL DEFAULT '{}'::jsonb,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (id, project_id),
    FOREIGN KEY (experiment_id, project_id) REFERENCES experiments (id, project_id) ON DELETE CASCADE
);

CREATE INDEX runs_project_status_idx ON runs (project_id, status);
CREATE INDEX runs_project_experiment_idx ON runs (project_id, experiment_id);
CREATE INDEX runs_git_sha_idx ON runs (git_sha);
CREATE INDEX runs_tags_gin_idx ON runs USING gin (tags);

CREATE TRIGGER runs_set_updated_at
    BEFORE UPDATE ON runs
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

CREATE TABLE sensors (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    project_id uuid NOT NULL,
    name text NOT NULL,
    type text NOT NULL,
    input_unit text NOT NULL,
    display_unit text NOT NULL,
    status sensor_status NOT NULL DEFAULT 'registering',
    token_hash bytea,
    token_preview text,
    last_heartbeat timestamptz,
    active_profile_id uuid,
    calibration_notes text,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (id, project_id)
);

CREATE UNIQUE INDEX sensors_project_name_uindex ON sensors (project_id, lower(name));
CREATE INDEX sensors_project_status_idx ON sensors (project_id, status);

CREATE TRIGGER sensors_set_updated_at
    BEFORE UPDATE ON sensors
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

-- Many-to-many relationship between sensors and projects.
-- Sensors keep sensors.project_id as the "primary" project for legacy joins and UX defaults.
CREATE TABLE sensor_projects (
    sensor_id uuid NOT NULL,
    project_id uuid NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (sensor_id, project_id),
    FOREIGN KEY (sensor_id) REFERENCES sensors (id) ON DELETE CASCADE,
    UNIQUE (sensor_id, project_id)
);

CREATE INDEX sensor_projects_project_idx ON sensor_projects (project_id);
CREATE INDEX sensor_projects_sensor_idx ON sensor_projects (sensor_id);

COMMENT ON TABLE sensor_projects IS 'Many-to-many relationship between sensors and projects. Each sensor can belong to multiple projects.';
COMMENT ON COLUMN sensors.project_id IS 'Primary project for backward compatibility. All projects are tracked in sensor_projects table.';

CREATE TABLE conversion_profiles (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    sensor_id uuid NOT NULL,
    project_id uuid NOT NULL,
    version text NOT NULL,
    kind text NOT NULL,
    payload jsonb NOT NULL DEFAULT '{}'::jsonb,
    status conversion_profile_status NOT NULL DEFAULT 'draft',
    valid_from timestamptz,
    valid_to timestamptz,
    created_by uuid NOT NULL,
    published_by uuid,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (sensor_id, version),
    FOREIGN KEY (sensor_id, project_id) REFERENCES sensors (id, project_id) ON DELETE CASCADE
);

CREATE INDEX conversion_profiles_project_status_idx ON conversion_profiles (project_id, status);

CREATE TRIGGER conversion_profiles_set_updated_at
    BEFORE UPDATE ON conversion_profiles
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

ALTER TABLE sensors
    ADD CONSTRAINT sensors_active_profile_fk
    FOREIGN KEY (active_profile_id) REFERENCES conversion_profiles (id) ON DELETE SET NULL;

CREATE TABLE run_sensors (
    run_id uuid NOT NULL,
    sensor_id uuid NOT NULL,
    project_id uuid NOT NULL,
    mode text NOT NULL DEFAULT 'primary',
    attached_at timestamptz NOT NULL DEFAULT now(),
    detached_at timestamptz,
    created_by uuid NOT NULL,
    PRIMARY KEY (run_id, sensor_id),
    FOREIGN KEY (run_id, project_id) REFERENCES runs (id, project_id) ON DELETE CASCADE,
    FOREIGN KEY (sensor_id, project_id) REFERENCES sensors (id, project_id) ON DELETE CASCADE
);

CREATE INDEX run_sensors_sensor_mode_idx ON run_sensors (sensor_id, mode);

CREATE TABLE capture_sessions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    run_id uuid NOT NULL,
    project_id uuid NOT NULL,
    ordinal_number integer NOT NULL CHECK (ordinal_number > 0),
    status capture_session_status NOT NULL DEFAULT 'draft',
    initiated_by uuid,
    notes text,
    started_at timestamptz,
    stopped_at timestamptz,
    archived boolean NOT NULL DEFAULT false,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (run_id, ordinal_number),
    FOREIGN KEY (run_id, project_id) REFERENCES runs (id, project_id) ON DELETE CASCADE
);

CREATE INDEX capture_sessions_project_status_idx ON capture_sessions (project_id, status);

CREATE TRIGGER capture_sessions_set_updated_at
    BEFORE UPDATE ON capture_sessions
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

CREATE TABLE capture_session_events (
    id bigserial PRIMARY KEY,
    capture_session_id uuid NOT NULL,
    event_type text NOT NULL,
    actor_id uuid NOT NULL,
    actor_role text NOT NULL,
    payload jsonb NOT NULL DEFAULT '{}'::jsonb,
    created_at timestamptz NOT NULL DEFAULT now(),
    FOREIGN KEY (capture_session_id) REFERENCES capture_sessions (id) ON DELETE CASCADE
);

CREATE INDEX capture_session_events_session_idx ON capture_session_events (capture_session_id, created_at DESC);

CREATE TABLE artifacts (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    run_id uuid NOT NULL,
    project_id uuid NOT NULL,
    type text NOT NULL,
    uri text NOT NULL,
    checksum text,
    size_bytes bigint,
    metadata jsonb NOT NULL DEFAULT '{}'::jsonb,
    created_by uuid NOT NULL,
    approved_by uuid,
    approval_note text,
    is_restricted boolean NOT NULL DEFAULT false,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    FOREIGN KEY (run_id, project_id) REFERENCES runs (id, project_id) ON DELETE CASCADE
);

CREATE INDEX artifacts_project_type_idx ON artifacts (project_id, type);

CREATE TRIGGER artifacts_set_updated_at
    BEFORE UPDATE ON artifacts
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

CREATE TABLE request_idempotency (
    idempotency_key text PRIMARY KEY,
    user_id uuid NOT NULL,
    request_path text NOT NULL,
    request_body_hash bytea NOT NULL,
    response_status integer NOT NULL,
    response_body jsonb NOT NULL DEFAULT '{}'::jsonb,
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX request_idempotency_user_idx ON request_idempotency (user_id, created_at DESC);

CREATE TABLE telemetry_records (
    -- NOTE: TimescaleDB requires that all UNIQUE indexes include:
    --  - the time column, and
    --  - the partitioning column (when using space partitioning).
    -- We keep an increasing id for debug/cursor tie-breaks; PK includes sensor_id + timestamp.
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
    FOREIGN KEY (capture_session_id) REFERENCES capture_sessions (id) ON DELETE SET NULL,
    FOREIGN KEY (conversion_profile_id) REFERENCES conversion_profiles (id)
);

-- Convert telemetry_records into a hypertable.
-- Chunking/partitioning defaults are a starting point; tune based on real ingestion volume.
SELECT create_hypertable(
    'telemetry_records',
    'timestamp',
    partitioning_column => 'sensor_id',
    number_partitions => 8,
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- Indexes for the main query patterns.
CREATE INDEX telemetry_records_project_sensor_ts_id_idx
    ON telemetry_records (project_id, sensor_id, timestamp ASC, id ASC);

CREATE INDEX telemetry_records_sensor_ts_idx
    ON telemetry_records (sensor_id, timestamp DESC, id DESC);

CREATE INDEX telemetry_records_run_ts_idx
    ON telemetry_records (run_id, timestamp DESC, id DESC);

CREATE INDEX telemetry_records_capture_ts_idx
    ON telemetry_records (capture_session_id, timestamp DESC, id DESC);

CREATE INDEX telemetry_records_sensor_signal_ts_idx
    ON telemetry_records (sensor_id, signal, timestamp DESC, id DESC);

-- TimescaleDB compression + retention (only in Community/TSL; skipped on Apache 2.0, e.g. Yandex MDB).
DO $$
BEGIN
    ALTER TABLE telemetry_records
        SET (
            timescaledb.compress,
            timescaledb.compress_segmentby = 'sensor_id, signal',
            timescaledb.compress_orderby = 'timestamp DESC'
        );
    PERFORM add_compression_policy('telemetry_records', INTERVAL '7 days');
    PERFORM add_retention_policy('telemetry_records', INTERVAL '90 days');
EXCEPTION
    WHEN OTHERS THEN
        RAISE NOTICE 'TimescaleDB compression/retention skipped (e.g. Apache 2.0 edition): %', SQLERRM;
END $$;

-- Webhooks schema (final, hardened).
CREATE TABLE webhook_subscriptions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    project_id uuid NOT NULL,
    target_url text NOT NULL,
    event_types text[] NOT NULL DEFAULT '{}'::text[],
    secret text,
    is_active boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX webhook_subscriptions_project_active_idx
    ON webhook_subscriptions (project_id, is_active);

CREATE INDEX webhook_subscriptions_event_types_gin_idx
    ON webhook_subscriptions USING gin (event_types);

CREATE TRIGGER webhook_subscriptions_set_updated_at
    BEFORE UPDATE ON webhook_subscriptions
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

CREATE TABLE webhook_deliveries (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    subscription_id uuid NOT NULL,
    project_id uuid NOT NULL,
    event_type text NOT NULL,
    target_url text NOT NULL,
    secret text,
    request_body jsonb NOT NULL DEFAULT '{}'::jsonb,
    dedup_key text UNIQUE,
    status text NOT NULL DEFAULT 'pending', -- pending|in_progress|succeeded|dead_lettered
    attempt_count integer NOT NULL DEFAULT 0,
    last_error text,
    locked_at timestamptz,
    next_attempt_at timestamptz NOT NULL DEFAULT now(),
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    FOREIGN KEY (subscription_id) REFERENCES webhook_subscriptions (id) ON DELETE CASCADE
);

CREATE INDEX webhook_deliveries_project_status_idx
    ON webhook_deliveries (project_id, status, created_at DESC);

CREATE INDEX webhook_deliveries_status_next_attempt_idx
    ON webhook_deliveries (status, next_attempt_at, created_at);

CREATE TRIGGER webhook_deliveries_set_updated_at
    BEFORE UPDATE ON webhook_deliveries
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

-- Audit log for run actions (status changes, bulk tag updates, etc.)
CREATE TABLE run_events (
    id bigserial PRIMARY KEY,
    run_id uuid NOT NULL,
    event_type text NOT NULL,
    actor_id uuid NOT NULL,
    actor_role text NOT NULL,
    payload jsonb NOT NULL DEFAULT '{}'::jsonb,
    created_at timestamptz NOT NULL DEFAULT now(),
    FOREIGN KEY (run_id) REFERENCES runs (id) ON DELETE CASCADE
);

CREATE INDEX run_events_run_idx ON run_events (run_id, created_at DESC);

CREATE TABLE run_metrics (
    id bigserial PRIMARY KEY,
    project_id uuid NOT NULL,
    run_id uuid NOT NULL,
    name text NOT NULL,
    step bigint NOT NULL,
    value double precision NOT NULL,
    timestamp timestamptz NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    FOREIGN KEY (run_id) REFERENCES runs (id) ON DELETE CASCADE
);

CREATE INDEX run_metrics_run_name_step_idx
    ON run_metrics (run_id, name, step);

CREATE INDEX run_metrics_project_name_idx
    ON run_metrics (project_id, name);

COMMIT;

