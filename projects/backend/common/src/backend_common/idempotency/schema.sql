CREATE TABLE IF NOT EXISTS idempotency_keys (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    idempotency_key VARCHAR(255) NOT NULL UNIQUE,
    user_id         VARCHAR(255) NOT NULL,
    request_path    TEXT NOT NULL,
    request_hash    VARCHAR(64) NOT NULL,
    response_status INTEGER,
    response_body   JSONB,
    completed       BOOLEAN NOT NULL DEFAULT false,
    expires_at      TIMESTAMPTZ NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_idempotency_keys_expires_at
    ON idempotency_keys (expires_at);

CREATE INDEX IF NOT EXISTS idx_idempotency_keys_user_created
    ON idempotency_keys (user_id, created_at DESC);
