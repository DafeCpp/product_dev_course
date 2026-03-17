-- script_service test schema
-- Adapted from migrations/001_initial_schema.sql for yandex-taxi-testsuite.
-- CREATE EXTENSION is omitted — testsuite creates extensions automatically.
-- ENUMs are created outside the transaction because PostgreSQL does not allow
-- CREATE TYPE inside a transaction that already has DDL statements in some
-- versions, but wrapping everything in BEGIN/COMMIT keeps the schema atomic.

BEGIN;

CREATE TYPE script_type AS ENUM ('python', 'bash', 'javascript');
CREATE TYPE execution_status AS ENUM ('pending', 'running', 'completed', 'failed', 'cancelled', 'timeout');

CREATE TABLE IF NOT EXISTS scripts (
    id                UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name              TEXT NOT NULL UNIQUE,
    description       TEXT,
    target_service    TEXT NOT NULL,
    script_type       script_type NOT NULL DEFAULT 'python',
    script_body       TEXT NOT NULL,
    parameters_schema JSONB NOT NULL DEFAULT '{}',
    timeout_sec       INTEGER NOT NULL DEFAULT 30,
    is_active         BOOLEAN NOT NULL DEFAULT TRUE,
    created_by        UUID NOT NULL,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at        TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS script_executions (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    script_id       UUID NOT NULL REFERENCES scripts(id),
    status          execution_status NOT NULL DEFAULT 'pending',
    parameters      JSONB NOT NULL DEFAULT '{}',
    target_instance TEXT,
    requested_by    UUID NOT NULL,
    started_at      TIMESTAMPTZ,
    finished_at     TIMESTAMPTZ,
    exit_code       INTEGER,
    stdout          TEXT,
    stderr          TEXT,
    error_message   TEXT,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS schema_migrations (
    version     TEXT PRIMARY KEY,
    checksum    TEXT NOT NULL DEFAULT '',
    applied_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_scripts_target_service ON scripts(target_service);
CREATE INDEX IF NOT EXISTS idx_scripts_is_active ON scripts(is_active);
CREATE INDEX IF NOT EXISTS idx_executions_script_id ON script_executions(script_id);
CREATE INDEX IF NOT EXISTS idx_executions_status ON script_executions(status);
CREATE INDEX IF NOT EXISTS idx_executions_requested_by ON script_executions(requested_by);
CREATE INDEX IF NOT EXISTS idx_executions_created_at ON script_executions(created_at DESC);

COMMIT;
