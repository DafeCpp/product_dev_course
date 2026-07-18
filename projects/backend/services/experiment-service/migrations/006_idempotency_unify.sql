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
-- over expired rows.
--
-- The backfill hard-codes the 48h that settings.idempotency_ttl_hours defaults to,
-- because SQL cannot read the service config. No deployment overrides that setting
-- today, so existing rows keep the exact expiry the cleanup worker gave them. If a
-- deployment ever does override it, this interval has to be changed to match before
-- the migration runs, or old rows will expire on the wrong schedule.
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

-- Generation of the current reservation. Reserving an expired row installs a fresh
-- token, so an owner whose request outlived the TTL can no longer write its response
-- into the row a retry has since taken over. Existing rows are all completed, so any
-- token will do — they will never be completed again.
ALTER TABLE request_idempotency
    ADD COLUMN IF NOT EXISTS reservation_token uuid;
UPDATE request_idempotency
    SET reservation_token = gen_random_uuid()
    WHERE reservation_token IS NULL;
ALTER TABLE request_idempotency
    ALTER COLUMN reservation_token SET NOT NULL;

-- Scope the key per user: the same Idempotency-Key from a different user is an
-- independent request, not a conflict. Existing keys were globally unique, so
-- the composite key cannot collide.
ALTER TABLE request_idempotency
    DROP CONSTRAINT IF EXISTS request_idempotency_pkey;
ALTER TABLE request_idempotency
    ADD PRIMARY KEY (idempotency_key, user_id);

CREATE INDEX IF NOT EXISTS request_idempotency_expires_at_idx
    ON request_idempotency (expires_at);
