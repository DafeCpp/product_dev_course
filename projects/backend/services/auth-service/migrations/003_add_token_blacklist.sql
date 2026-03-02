-- 003_add_token_blacklist.sql
-- Таблица для хранения отозванных refresh-токенов (JWT blacklist).

BEGIN;

CREATE TABLE revoked_tokens (
    jti        uuid        PRIMARY KEY,
    user_id    uuid        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    expires_at timestamptz NOT NULL,
    revoked_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX revoked_tokens_expires_at_idx ON revoked_tokens (expires_at);

COMMIT;
