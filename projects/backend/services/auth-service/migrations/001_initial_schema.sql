-- 001_initial_schema.sql
-- Full Auth Service schema (merged from 001-007).
-- pgcrypto создаётся при создании БД (Terraform / init script), не миграцией.

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
    is_admin BOOLEAN NOT NULL DEFAULT false,
    is_active BOOLEAN NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX users_username_idx ON users (username);
CREATE INDEX users_email_idx ON users (email);
CREATE INDEX users_username_lower_idx ON users (lower(username));
CREATE INDEX users_is_active_idx ON users (is_active);

CREATE TRIGGER users_set_updated_at
    BEFORE UPDATE ON users
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

CREATE TABLE projects (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    name text NOT NULL,
    description text,
    owner_id uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (name, owner_id)
);

CREATE INDEX projects_owner_id_idx ON projects (owner_id);
CREATE INDEX projects_name_idx ON projects (name);

CREATE TRIGGER projects_set_updated_at
    BEFORE UPDATE ON projects
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

CREATE TABLE project_members (
    project_id uuid NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    user_id uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role text NOT NULL DEFAULT 'viewer' CHECK (role IN ('owner', 'editor', 'viewer')),
    created_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (project_id, user_id)
);

CREATE INDEX project_members_user_id_idx ON project_members (user_id);
CREATE INDEX project_members_project_id_idx ON project_members (project_id);
CREATE INDEX project_members_role_idx ON project_members (role);

CREATE OR REPLACE FUNCTION add_project_owner_as_member()
RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO project_members (project_id, user_id, role)
    VALUES (NEW.id, NEW.owner_id, 'owner')
    ON CONFLICT (project_id, user_id) DO NOTHING;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER projects_add_owner_member
    AFTER INSERT ON projects
    FOR EACH ROW
    EXECUTE FUNCTION add_project_owner_as_member();

CREATE TABLE revoked_tokens (
    jti        uuid        PRIMARY KEY,
    user_id    uuid        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    expires_at timestamptz NOT NULL,
    revoked_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX revoked_tokens_expires_at_idx ON revoked_tokens (expires_at);

CREATE TABLE password_reset_tokens (
    token      text        PRIMARY KEY,
    user_id    uuid        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    expires_at timestamptz NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX password_reset_tokens_user_id_idx ON password_reset_tokens (user_id);

CREATE TABLE invite_tokens (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    token       uuid        NOT NULL UNIQUE DEFAULT gen_random_uuid(),
    created_by  uuid        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    email_hint  text,
    expires_at  timestamptz NOT NULL,
    used_at     timestamptz,
    used_by     uuid        REFERENCES users(id) ON DELETE SET NULL,
    created_at  timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX invite_tokens_token_idx ON invite_tokens (token);
CREATE INDEX invite_tokens_created_by_idx ON invite_tokens (created_by);

CREATE TABLE IF NOT EXISTS schema_migrations (
    version text PRIMARY KEY,
    checksum text NOT NULL,
    applied_at timestamptz NOT NULL DEFAULT now()
);

COMMIT;
