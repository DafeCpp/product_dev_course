-- 001_initial_schema.sql
-- Initial Experiment Service schema (Foundation scope).

BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;

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

COMMIT;

