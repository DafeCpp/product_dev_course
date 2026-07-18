BEGIN;

-- Configs table
CREATE TABLE IF NOT EXISTS configs (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    service_name    VARCHAR(128) NOT NULL,
    project_id      VARCHAR(128),
    key             VARCHAR(128) NOT NULL,
    config_type     VARCHAR(32) NOT NULL,
    description     TEXT,
    value           JSONB NOT NULL DEFAULT '{}',
    metadata        JSONB NOT NULL DEFAULT '{}',
    is_active       BOOLEAN NOT NULL DEFAULT true,
    is_critical     BOOLEAN NOT NULL DEFAULT false,
    is_sensitive    BOOLEAN NOT NULL DEFAULT false,
    version         INTEGER NOT NULL DEFAULT 1,
    created_by      VARCHAR(255) NOT NULL,
    updated_by      VARCHAR(255) NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    deleted_at      TIMESTAMPTZ
);

-- Unique constraint: only one active (non-deleted) record per (service, project, key)
CREATE UNIQUE INDEX IF NOT EXISTS idx_configs_unique_active
    ON configs (service_name, COALESCE(project_id, ''), key)
    WHERE deleted_at IS NULL;

-- Fast lookup of active configs for bulk endpoint
CREATE INDEX IF NOT EXISTS idx_configs_active
    ON configs (service_name, project_id, is_active)
    WHERE is_active = true AND deleted_at IS NULL;

CREATE INDEX IF NOT EXISTS idx_configs_service
    ON configs (service_name)
    WHERE deleted_at IS NULL;

-- Auto-update updated_at
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS configs_updated_at ON configs;
CREATE TRIGGER configs_updated_at
    BEFORE UPDATE ON configs
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- Config history table
CREATE TABLE IF NOT EXISTS config_history (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    config_id       UUID NOT NULL REFERENCES configs(id),
    version         INTEGER NOT NULL,
    service_name    VARCHAR(128) NOT NULL,
    key             VARCHAR(128) NOT NULL,
    config_type     VARCHAR(32) NOT NULL,
    value           JSONB NOT NULL DEFAULT '{}',
    metadata        JSONB NOT NULL DEFAULT '{}',
    is_active       BOOLEAN NOT NULL,
    changed_by      VARCHAR(255) NOT NULL,
    change_reason   TEXT,
    source_ip       VARCHAR(45),
    user_agent      TEXT,
    correlation_id  VARCHAR(128),
    changed_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_config_history_config_id
    ON config_history (config_id, version DESC);

-- Config schemas table
CREATE TABLE IF NOT EXISTS config_schemas (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    config_type VARCHAR(32) NOT NULL,
    schema      JSONB NOT NULL,
    version     INTEGER NOT NULL,
    is_active   BOOLEAN NOT NULL DEFAULT true,
    created_by  VARCHAR(255) NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Only one active schema per config_type at a time
CREATE UNIQUE INDEX IF NOT EXISTS idx_config_schemas_active_unique
    ON config_schemas (config_type)
    WHERE is_active = true;

CREATE INDEX IF NOT EXISTS idx_config_schemas_type
    ON config_schemas (config_type, version DESC);

-- Idempotency keys table
CREATE TABLE IF NOT EXISTS idempotency_keys (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    idempotency_key VARCHAR(255) NOT NULL UNIQUE,
    user_id         VARCHAR(255) NOT NULL,
    request_path    TEXT NOT NULL,
    request_hash    VARCHAR(64) NOT NULL,
    response_status INTEGER NOT NULL,
    response_body   JSONB NOT NULL,
    expires_at      TIMESTAMPTZ NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_idempotency_keys_expires_at
    ON idempotency_keys (expires_at);

COMMIT;
