-- Canonical shape of the idempotency table used by backend_common.idempotency.
-- Services may name the table differently (pass table_name= to the repository),
-- but the columns and the (idempotency_key, user_id) key must match.
--
-- The key is scoped per user: the same Idempotency-Key sent by two different
-- users describes two independent requests.

CREATE TABLE IF NOT EXISTS idempotency_keys (
    idempotency_key   VARCHAR(255) NOT NULL,
    user_id           VARCHAR(255) NOT NULL,
    request_path      TEXT NOT NULL,
    request_hash      VARCHAR(64) NOT NULL,    -- sha256 hex of the canonical body
    response_status   INTEGER,                 -- NULL while the reservation is pending
    response_body     JSONB,                   -- NULL while the reservation is pending
    completed         BOOLEAN NOT NULL DEFAULT false,
    -- Generation of the current reservation. Reserving an expired row installs a
    -- fresh token, so an owner that outlived its TTL can no longer complete the
    -- row the retry now holds.
    reservation_token UUID NOT NULL,
    expires_at        TIMESTAMPTZ NOT NULL,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (idempotency_key, user_id)
);

-- Supports the TTL cleanup worker and the expired-row takeover in reserve().
CREATE INDEX IF NOT EXISTS idx_idempotency_keys_expires_at
    ON idempotency_keys (expires_at);
