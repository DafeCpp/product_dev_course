-- Converge idempotency_keys on the shared backend_common.idempotency layout.

-- Reservation-first: a pending row is inserted before the mutation runs, so the
-- response columns are only filled in once it succeeds.
ALTER TABLE idempotency_keys
    ALTER COLUMN response_status DROP NOT NULL,
    ALTER COLUMN response_body DROP NOT NULL,
    ADD COLUMN IF NOT EXISTS completed BOOLEAN NOT NULL DEFAULT false;

-- Every row written before this migration holds a finished response.
UPDATE idempotency_keys SET completed = true WHERE completed = false;

-- Generation of the current reservation. Reserving an expired row installs a fresh
-- token, so an owner whose request outlived the TTL can no longer write its response
-- into the row a retry has since taken over. Existing rows are all completed, so any
-- token will do — they will never be completed again.
ALTER TABLE idempotency_keys
    ADD COLUMN IF NOT EXISTS reservation_token UUID;
UPDATE idempotency_keys
    SET reservation_token = gen_random_uuid()
    WHERE reservation_token IS NULL;
ALTER TABLE idempotency_keys
    ALTER COLUMN reservation_token SET NOT NULL;

-- Scope the key per user: the same Idempotency-Key from a different user is an
-- independent request, not a conflict. This is the behaviour config-service
-- already promised; it now lives in the schema instead of in service code.
-- Keys were globally unique before, so the composite key cannot collide.
-- Dropping the unused surrogate id takes its primary key with it.
ALTER TABLE idempotency_keys
    DROP CONSTRAINT IF EXISTS idempotency_keys_idempotency_key_key;
ALTER TABLE idempotency_keys
    DROP COLUMN IF EXISTS id;
ALTER TABLE idempotency_keys
    ADD PRIMARY KEY (idempotency_key, user_id);
