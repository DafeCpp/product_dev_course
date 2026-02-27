-- 001_initial_schema.sql
-- Initial Auth Service schema with default admin user.

-- pgcrypto создаётся при создании БД (Terraform / init script), не миграцией (нет прав у app user).
BEGIN;

CREATE OR REPLACE FUNCTION set_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = now();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TABLE users (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    username text NOT NULL UNIQUE,
    email text NOT NULL UNIQUE,
    hashed_password text NOT NULL,
    password_change_required BOOLEAN NOT NULL DEFAULT false,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX users_username_idx ON users (username);
CREATE INDEX users_email_idx ON users (email);
CREATE INDEX users_username_lower_idx ON users (lower(username));

CREATE TRIGGER users_set_updated_at
    BEFORE UPDATE ON users
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

-- Создаём пользователя по умолчанию (пароль: admin123)
-- ВАЖНО: В production обязательно смените пароль при первом входе!
-- Хеш пароля "admin123" с bcrypt (12 rounds)
INSERT INTO users (username, email, hashed_password, password_change_required)
VALUES (
    'admin',
    'admin@example.com',
    '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG',  -- admin123
    true  -- Требуется смена пароля при первом входе
)
ON CONFLICT (username) DO NOTHING;

-- Schema migrations tracking
CREATE TABLE IF NOT EXISTS schema_migrations (
    version text PRIMARY KEY,
    checksum text NOT NULL,
    applied_at timestamptz NOT NULL DEFAULT now()
);

COMMIT;

