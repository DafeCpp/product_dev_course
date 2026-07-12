-- Converge request_idempotency on the shared backend_common.idempotency layout.
-- The table keeps its name (the repository is pointed at it via table_name=).

-- The shared service hashes the canonical body to a sha256 hex string.
ALTER TABLE request_idempotency
    ALTER COLUMN request_body_hash TYPE varchar(64) USING encode(request_body_hash, 'hex');
ALTER TABLE request_idempotency
    RENAME COLUMN request_body_hash TO request_hash;

-- user_id is text in the shared repository: services disagree on whether the
-- subject is a uuid (experiment) or an opaque string (config).
ALTER TABLE request_idempotency
    ALTER COLUMN user_id TYPE varchar(255) USING user_id::text;

-- TTL moves from "created_at older than idempotency_ttl_hours" (enforced by the
-- cleanup worker) to an explicit expires_at, which reserve() also uses to take
-- over expired rows. Backfill preserves the current 48h expiry of existing rows.
ALTER TABLE request_idempotency
    ADD COLUMN IF NOT EXISTS expires_at timestamptz;
UPDATE request_idempotency
    SET expires_at = created_at + interval '48 hours'
    WHERE expires_at IS NULL;
ALTER TABLE request_idempotency
    ALTER COLUMN expires_at SET NOT NULL;

-- A pending reservation carries no response yet, so the placeholder must be NULL.
ALTER TABLE request_idempotency
    ALTER COLUMN response_body DROP DEFAULT;

-- Scope the key per user: the same Idempotency-Key from a different user is an
-- independent request, not a conflict. Existing keys were globally unique, so
-- the composite key cannot collide.
ALTER TABLE request_idempotency
    DROP CONSTRAINT IF EXISTS request_idempotency_pkey;
ALTER TABLE request_idempotency
    ADD PRIMARY KEY (idempotency_key, user_id);

CREATE INDEX IF NOT EXISTS request_idempotency_expires_at_idx
    ON request_idempotency (expires_at);
