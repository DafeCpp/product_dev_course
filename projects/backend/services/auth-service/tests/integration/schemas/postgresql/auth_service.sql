-- 001_initial_schema.sql
-- Auth Service schema with RBAC v2.
-- pgcrypto создаётся при создании БД (Terraform / init script), не миграцией.

BEGIN;

-- Drop all tables with CASCADE to handle foreign key dependencies
DROP TABLE IF EXISTS schema_migrations CASCADE;
DROP TABLE IF EXISTS invite_tokens CASCADE;
DROP TABLE IF EXISTS password_reset_tokens CASCADE;
DROP TABLE IF EXISTS revoked_tokens CASCADE;
DROP TABLE IF EXISTS refresh_token_families CASCADE;
DROP TABLE IF EXISTS audit_log CASCADE;
DROP TABLE IF EXISTS user_project_roles CASCADE;
DROP TABLE IF EXISTS user_system_roles CASCADE;
DROP TABLE IF EXISTS role_permissions CASCADE;
DROP TABLE IF EXISTS roles CASCADE;
DROP TABLE IF EXISTS permissions CASCADE;
DROP TABLE IF EXISTS projects CASCADE;
DROP TABLE IF EXISTS users CASCADE;

-- =============================================================================
-- Utility functions
-- =============================================================================

CREATE OR REPLACE FUNCTION set_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = now();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- =============================================================================
-- Core tables
-- =============================================================================

CREATE TABLE users (
    id                      uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    username                text NOT NULL UNIQUE,
    email                   text NOT NULL UNIQUE,
    hashed_password         text NOT NULL,
    password_change_required BOOLEAN NOT NULL DEFAULT false,
    -- is_admin убран: заменён системной ролью superadmin
    is_active               BOOLEAN NOT NULL DEFAULT true,
    created_at              timestamptz NOT NULL DEFAULT now(),
    updated_at              timestamptz NOT NULL DEFAULT now()
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
    id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    name        text NOT NULL,
    description text,
    owner_id    uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now(),
    UNIQUE (name, owner_id)
);

CREATE INDEX projects_owner_id_idx ON projects (owner_id);
CREATE INDEX projects_name_idx ON projects (name);

CREATE TRIGGER projects_set_updated_at
    BEFORE UPDATE ON projects
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

-- =============================================================================
-- RBAC v2: permissions, roles, assignments
-- =============================================================================

-- Справочник атомарных permissions (seed-данные ниже)
CREATE TABLE permissions (
    id          text PRIMARY KEY,               -- 'experiments.create', 'scripts.execute'
    scope_type  text NOT NULL CHECK (scope_type IN ('system', 'project')),
    category    text NOT NULL,                  -- 'experiments', 'scripts', 'users', etc.
    description text,
    created_at  timestamptz NOT NULL DEFAULT now()
);

-- Роли: встроенные (is_builtin=true) + кастомные
CREATE TABLE roles (
    id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    name        text NOT NULL,
    scope_type  text NOT NULL CHECK (scope_type IN ('system', 'project')),
    project_id  uuid REFERENCES projects(id) ON DELETE CASCADE,  -- NULL для system/шаблонных project ролей
    is_builtin  BOOLEAN NOT NULL DEFAULT false,
    description text,
    created_by  uuid REFERENCES users(id) ON DELETE SET NULL,
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now(),

    -- Уникальность имени: для system — глобально, для project — в рамках проекта
    UNIQUE NULLS NOT DISTINCT (name, scope_type, project_id)
);

CREATE INDEX roles_scope_type_idx ON roles (scope_type);
CREATE INDEX roles_project_id_idx ON roles (project_id);

CREATE TRIGGER roles_set_updated_at
    BEFORE UPDATE ON roles
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

-- Связь ролей с permissions (many-to-many)
CREATE TABLE role_permissions (
    role_id       uuid NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    permission_id text NOT NULL REFERENCES permissions(id) ON DELETE CASCADE,
    PRIMARY KEY (role_id, permission_id)
);

CREATE INDEX role_permissions_permission_idx ON role_permissions (permission_id);

-- Назначение системных ролей пользователям
CREATE TABLE user_system_roles (
    user_id    uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role_id    uuid NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    granted_by uuid NOT NULL REFERENCES users(id),
    granted_at timestamptz NOT NULL DEFAULT now(),
    expires_at timestamptz,                     -- NULL = бессрочно
    PRIMARY KEY (user_id, role_id)
);

CREATE INDEX user_system_roles_role_idx ON user_system_roles (role_id);

-- Назначение проектных ролей пользователям (замена project_members)
CREATE TABLE user_project_roles (
    user_id    uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    project_id uuid NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    role_id    uuid NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    granted_by uuid NOT NULL REFERENCES users(id),
    granted_at timestamptz NOT NULL DEFAULT now(),
    expires_at timestamptz,                     -- NULL = бессрочно
    PRIMARY KEY (user_id, project_id, role_id)
);

CREATE INDEX user_project_roles_project_idx ON user_project_roles (project_id);
CREATE INDEX user_project_roles_user_idx ON user_project_roles (user_id);
CREATE INDEX user_project_roles_role_idx ON user_project_roles (role_id);

-- =============================================================================
-- Audit log (append-only)
-- =============================================================================

CREATE TABLE audit_log (
    id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    timestamp   timestamptz NOT NULL DEFAULT now(),
    actor_id    uuid NOT NULL,                  -- кто совершил действие
    action      text NOT NULL,                  -- 'role.grant', 'experiment.create', 'script.execute'
    scope_type  text NOT NULL,                  -- 'system' | 'project'
    scope_id    uuid,                           -- project_id или NULL для system
    target_type text,                           -- 'user', 'experiment', 'script', 'role'
    target_id   text,                           -- id цели действия
    details     jsonb NOT NULL DEFAULT '{}',    -- произвольные данные
    ip_address  inet,
    user_agent  text
);

CREATE INDEX audit_log_actor_idx ON audit_log (actor_id);
CREATE INDEX audit_log_action_idx ON audit_log (action);
CREATE INDEX audit_log_scope_idx ON audit_log (scope_type, scope_id);
CREATE INDEX audit_log_timestamp_idx ON audit_log (timestamp DESC);
CREATE INDEX audit_log_target_idx ON audit_log (target_type, target_id);

-- =============================================================================
-- Auth tokens
-- =============================================================================

CREATE TABLE refresh_token_families (
    id         UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id    UUID        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    revoked_at TIMESTAMPTZ
);

CREATE INDEX idx_rtf_user ON refresh_token_families(user_id);

CREATE TABLE revoked_tokens (
    jti        uuid        PRIMARY KEY,
    user_id    uuid        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    expires_at timestamptz NOT NULL,
    revoked_at timestamptz NOT NULL DEFAULT now(),
    family_id  uuid        REFERENCES refresh_token_families(id)
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

-- =============================================================================
-- Schema migrations tracking
-- =============================================================================

CREATE TABLE IF NOT EXISTS schema_migrations (
    version    text PRIMARY KEY,
    checksum   text NOT NULL,
    applied_at timestamptz NOT NULL DEFAULT now()
);

-- =============================================================================
-- Seed: built-in permissions
-- =============================================================================

-- System permissions
INSERT INTO permissions (id, scope_type, category, description) VALUES
    -- users
    ('users.list',           'system', 'users',   'Просмотр списка пользователей'),
    ('users.create',         'system', 'users',   'Создание пользователей (инвайты)'),
    ('users.update',         'system', 'users',   'Изменение данных пользователей'),
    ('users.deactivate',     'system', 'users',   'Деактивация пользователей'),
    ('users.delete',         'system', 'users',   'Удаление пользователей'),
    ('users.reset_password', 'system', 'users',   'Сброс пароля другого пользователя'),
    -- roles
    ('roles.manage',         'system', 'roles',   'Создание/редактирование кастомных системных ролей'),
    ('roles.assign',         'system', 'roles',   'Назначение системных ролей пользователям'),
    -- scripts
    ('scripts.manage',       'system', 'scripts', 'Создание/редактирование/удаление скриптов'),
    ('scripts.execute',      'system', 'scripts', 'Запуск скриптов на сервисах'),
    ('scripts.view_logs',    'system', 'scripts', 'Просмотр логов выполнения скриптов'),
    -- configs
    ('configs.read',         'system', 'configs', 'Просмотр динамических конфигов'),
    ('configs.write',        'system', 'configs', 'Изменение динамических конфигов'),
    ('configs.publish',      'system', 'configs', 'Публикация конфигов на сервисы'),
    -- audit
    ('audit.read',           'system', 'audit',   'Просмотр аудит-лога'),
    -- projects
    ('projects.create',      'system', 'projects','Создание проектов');

-- Project permissions
INSERT INTO permissions (id, scope_type, category, description) VALUES
    -- project settings
    ('project.settings.update', 'project', 'settings',   'Изменение настроек проекта'),
    ('project.settings.delete', 'project', 'settings',   'Удаление проекта'),
    -- project members
    ('project.members.view',        'project', 'members', 'Просмотр участников проекта'),
    ('project.members.invite',      'project', 'members', 'Приглашение участников в проект'),
    ('project.members.remove',      'project', 'members', 'Удаление участников из проекта'),
    ('project.members.change_role', 'project', 'members', 'Изменение ролей участников проекта'),
    -- project roles
    ('project.roles.manage',        'project', 'roles',   'Создание кастомных ролей в проекте'),
    -- experiments
    ('experiments.view',    'project', 'experiments', 'Просмотр экспериментов'),
    ('experiments.create',  'project', 'experiments', 'Создание экспериментов'),
    ('experiments.update',  'project', 'experiments', 'Редактирование экспериментов'),
    ('experiments.delete',  'project', 'experiments', 'Удаление экспериментов'),
    ('experiments.archive', 'project', 'experiments', 'Архивация экспериментов'),
    -- runs
    ('runs.create', 'project', 'runs', 'Создание ранов'),
    ('runs.update', 'project', 'runs', 'Обновление ранов'),
    -- sensors
    ('sensors.view',         'project', 'sensors', 'Просмотр сенсоров'),
    ('sensors.manage',       'project', 'sensors', 'Создание/настройка сенсоров'),
    ('sensors.rotate_token', 'project', 'sensors', 'Ротация токенов сенсоров'),
    -- conversion profiles
    ('conversion_profiles.manage',  'project', 'conversion', 'Управление профилями конвертации'),
    ('conversion_profiles.publish', 'project', 'conversion', 'Публикация профилей конвертации'),
    -- webhooks
    ('webhooks.manage',          'project', 'webhooks', 'Управление вебхуками'),
    -- capture sessions
    ('capture_sessions.manage',  'project', 'capture',  'Управление capture sessions'),
    -- backfill
    ('backfill.create',          'project', 'backfill', 'Создание backfill задач');

-- =============================================================================
-- Seed: built-in system roles
-- =============================================================================

-- Используем фиксированные UUID для встроенных ролей, чтобы можно было ссылаться из кода
INSERT INTO roles (id, name, scope_type, project_id, is_builtin, description) VALUES
    ('00000000-0000-0000-0000-000000000001', 'superadmin', 'system', NULL, true,
     'Суперпользователь. Все permissions неявно. Замена is_admin.'),
    ('00000000-0000-0000-0000-000000000002', 'admin', 'system', NULL, true,
     'Управление пользователями и ролями.'),
    ('00000000-0000-0000-0000-000000000003', 'operator', 'system', NULL, true,
     'Оператор: скрипты, конфиги, мониторинг.'),
    ('00000000-0000-0000-0000-000000000004', 'auditor', 'system', NULL, true,
     'Только чтение: аудит и список пользователей.');

-- admin: users.*, roles.*, audit.read, projects.create
INSERT INTO role_permissions (role_id, permission_id) VALUES
    ('00000000-0000-0000-0000-000000000002', 'users.list'),
    ('00000000-0000-0000-0000-000000000002', 'users.create'),
    ('00000000-0000-0000-0000-000000000002', 'users.update'),
    ('00000000-0000-0000-0000-000000000002', 'users.deactivate'),
    ('00000000-0000-0000-0000-000000000002', 'users.delete'),
    ('00000000-0000-0000-0000-000000000002', 'users.reset_password'),
    ('00000000-0000-0000-0000-000000000002', 'roles.manage'),
    ('00000000-0000-0000-0000-000000000002', 'roles.assign'),
    ('00000000-0000-0000-0000-000000000002', 'audit.read'),
    ('00000000-0000-0000-0000-000000000002', 'projects.create');

-- operator: scripts.*, configs.*, audit.read
INSERT INTO role_permissions (role_id, permission_id) VALUES
    ('00000000-0000-0000-0000-000000000003', 'scripts.manage'),
    ('00000000-0000-0000-0000-000000000003', 'scripts.execute'),
    ('00000000-0000-0000-0000-000000000003', 'scripts.view_logs'),
    ('00000000-0000-0000-0000-000000000003', 'configs.read'),
    ('00000000-0000-0000-0000-000000000003', 'configs.write'),
    ('00000000-0000-0000-0000-000000000003', 'configs.publish'),
    ('00000000-0000-0000-0000-000000000003', 'audit.read');

-- auditor: audit.read, users.list
INSERT INTO role_permissions (role_id, permission_id) VALUES
    ('00000000-0000-0000-0000-000000000004', 'audit.read'),
    ('00000000-0000-0000-0000-000000000004', 'users.list');

-- superadmin НЕ имеет записей в role_permissions — все permissions неявно (проверяется в коде)

-- =============================================================================
-- Seed: built-in project roles (шаблоны, project_id=NULL)
-- =============================================================================

INSERT INTO roles (id, name, scope_type, project_id, is_builtin, description) VALUES
    ('00000000-0000-0000-0000-000000000010', 'owner', 'project', NULL, true,
     'Владелец проекта. Все project permissions.'),
    ('00000000-0000-0000-0000-000000000011', 'editor', 'project', NULL, true,
     'Редактор. Все кроме удаления, управления участниками и публикации.'),
    ('00000000-0000-0000-0000-000000000012', 'viewer', 'project', NULL, true,
     'Наблюдатель. Только просмотр.');

-- owner: все project permissions
INSERT INTO role_permissions (role_id, permission_id) VALUES
    ('00000000-0000-0000-0000-000000000010', 'project.settings.update'),
    ('00000000-0000-0000-0000-000000000010', 'project.settings.delete'),
    ('00000000-0000-0000-0000-000000000010', 'project.members.view'),
    ('00000000-0000-0000-0000-000000000010', 'project.members.invite'),
    ('00000000-0000-0000-0000-000000000010', 'project.members.remove'),
    ('00000000-0000-0000-0000-000000000010', 'project.members.change_role'),
    ('00000000-0000-0000-0000-000000000010', 'project.roles.manage'),
    ('00000000-0000-0000-0000-000000000010', 'experiments.view'),
    ('00000000-0000-0000-0000-000000000010', 'experiments.create'),
    ('00000000-0000-0000-0000-000000000010', 'experiments.update'),
    ('00000000-0000-0000-0000-000000000010', 'experiments.delete'),
    ('00000000-0000-0000-0000-000000000010', 'experiments.archive'),
    ('00000000-0000-0000-0000-000000000010', 'runs.create'),
    ('00000000-0000-0000-0000-000000000010', 'runs.update'),
    ('00000000-0000-0000-0000-000000000010', 'sensors.view'),
    ('00000000-0000-0000-0000-000000000010', 'sensors.manage'),
    ('00000000-0000-0000-0000-000000000010', 'sensors.rotate_token'),
    ('00000000-0000-0000-0000-000000000010', 'conversion_profiles.manage'),
    ('00000000-0000-0000-0000-000000000010', 'conversion_profiles.publish'),
    ('00000000-0000-0000-0000-000000000010', 'webhooks.manage'),
    ('00000000-0000-0000-0000-000000000010', 'capture_sessions.manage'),
    ('00000000-0000-0000-0000-000000000010', 'backfill.create');

-- editor: всё кроме *.delete, members.remove, members.change_role, roles.manage, conversion_profiles.publish
INSERT INTO role_permissions (role_id, permission_id) VALUES
    ('00000000-0000-0000-0000-000000000011', 'project.settings.update'),
    ('00000000-0000-0000-0000-000000000011', 'project.members.view'),
    ('00000000-0000-0000-0000-000000000011', 'project.members.invite'),
    ('00000000-0000-0000-0000-000000000011', 'experiments.view'),
    ('00000000-0000-0000-0000-000000000011', 'experiments.create'),
    ('00000000-0000-0000-0000-000000000011', 'experiments.update'),
    ('00000000-0000-0000-0000-000000000011', 'experiments.archive'),
    ('00000000-0000-0000-0000-000000000011', 'runs.create'),
    ('00000000-0000-0000-0000-000000000011', 'runs.update'),
    ('00000000-0000-0000-0000-000000000011', 'sensors.view'),
    ('00000000-0000-0000-0000-000000000011', 'sensors.manage'),
    ('00000000-0000-0000-0000-000000000011', 'sensors.rotate_token'),
    ('00000000-0000-0000-0000-000000000011', 'conversion_profiles.manage'),
    ('00000000-0000-0000-0000-000000000011', 'webhooks.manage'),
    ('00000000-0000-0000-0000-000000000011', 'capture_sessions.manage'),
    ('00000000-0000-0000-0000-000000000011', 'backfill.create');

-- viewer: только просмотр
INSERT INTO role_permissions (role_id, permission_id) VALUES
    ('00000000-0000-0000-0000-000000000012', 'project.members.view'),
    ('00000000-0000-0000-0000-000000000012', 'experiments.view'),
    ('00000000-0000-0000-0000-000000000012', 'sensors.view');

-- =============================================================================
-- Trigger: автоматическое назначение роли owner при создании проекта
-- =============================================================================

CREATE OR REPLACE FUNCTION assign_project_owner_role()
RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO user_project_roles (user_id, project_id, role_id, granted_by)
    VALUES (
        NEW.owner_id,
        NEW.id,
        '00000000-0000-0000-0000-000000000010',  -- built-in owner role
        NEW.owner_id                              -- self-granted при создании
    )
    ON CONFLICT (user_id, project_id, role_id) DO NOTHING;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER projects_assign_owner_role
    AFTER INSERT ON projects
    FOR EACH ROW
    EXECUTE FUNCTION assign_project_owner_role();

COMMIT;
