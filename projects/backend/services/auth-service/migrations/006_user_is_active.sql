-- Добавление флага активности пользователя.

ALTER TABLE users ADD COLUMN IF NOT EXISTS is_active BOOLEAN NOT NULL DEFAULT true;
CREATE INDEX users_is_active_idx ON users (is_active);
