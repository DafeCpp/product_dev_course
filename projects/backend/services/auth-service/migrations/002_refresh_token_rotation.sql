-- 002_refresh_token_rotation.sql
-- Добавляет поддержку ротации refresh-токенов через token families.

BEGIN;

CREATE TABLE IF NOT EXISTS refresh_token_families (
    id         UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id    UUID        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    revoked_at TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS idx_rtf_user ON refresh_token_families(user_id);

ALTER TABLE revoked_tokens
    ADD COLUMN IF NOT EXISTS family_id UUID REFERENCES refresh_token_families(id);

COMMIT;
