-- =============================================================================
-- 003: config-service RBAC — fine-grained permissions + встроенные роли
-- =============================================================================
--
-- Контекст: config-service (LOS-42) проверяет на роутах fine-grained права
-- (configs.view/create/update/delete/activate/rollback/schemas.manage/
-- sensitive.read), но миграция 001 засеяла другой словарь
-- (configs.read/write/publish), который никто не проверяет. Из-за рассинхрона
-- non-superadmin не мог работать с config-service.
--
-- Здесь: (1) удаляем legacy-права, (2) заводим fine-grained права,
-- (3) создаём 4 встроенные системные роли с принципом «4 глаз»
-- (editor пишет, но не активирует; operator активирует, но не редактирует),
-- (4) перенацеливаем встроенную роль operator на новый словарь.

-- ── 1. Удаляем legacy config-права ──────────────────────────────────────────
-- role_permissions.permission_id имеет ON DELETE CASCADE → связи уйдут сами,
-- но удаляем явно для наглядности.
DELETE FROM role_permissions
    WHERE permission_id IN ('configs.read', 'configs.write', 'configs.publish');
DELETE FROM permissions
    WHERE id IN ('configs.read', 'configs.write', 'configs.publish');

-- ── 2. Fine-grained config-права (system scope) ─────────────────────────────
INSERT INTO permissions (id, scope_type, category, description) VALUES
    ('configs.view',           'system', 'configs', 'Просмотр конфигов, истории и схем'),
    ('configs.create',         'system', 'configs', 'Создание конфигов (вкл. dry-run)'),
    ('configs.update',         'system', 'configs', 'Редактирование конфигов (вкл. dry-run)'),
    ('configs.delete',         'system', 'configs', 'Удаление конфигов'),
    ('configs.activate',       'system', 'configs', 'Активация/деактивация конфигов'),
    ('configs.rollback',       'system', 'configs', 'Откат конфигов к предыдущей версии'),
    ('configs.schemas.manage', 'system', 'configs', 'Управление JSON-схемами конфигов'),
    ('configs.sensitive.read', 'system', 'configs', 'Просмотр незаредактированных sensitive-значений');

-- ── 3. Встроенные системные роли config-service ─────────────────────────────
INSERT INTO roles (id, name, scope_type, project_id, is_builtin, description) VALUES
    ('00000000-0000-0000-0000-000000000005', 'config_viewer',   'system', NULL, true,
     'Config-service: только чтение конфигов, истории и схем.'),
    ('00000000-0000-0000-0000-000000000006', 'config_editor',   'system', NULL, true,
     'Config-service: создание/редактирование/удаление черновиков (без активации).'),
    ('00000000-0000-0000-0000-000000000007', 'config_operator', 'system', NULL, true,
     'Config-service: активация/деактивация/откат (без права редактировать) — вторая пара глаз.'),
    ('00000000-0000-0000-0000-000000000008', 'config_admin',    'system', NULL, true,
     'Config-service: полный доступ, включая управление схемами и чтение sensitive.');

-- config_viewer
INSERT INTO role_permissions (role_id, permission_id) VALUES
    ('00000000-0000-0000-0000-000000000005', 'configs.view');

-- config_editor: view + create/update/delete
INSERT INTO role_permissions (role_id, permission_id) VALUES
    ('00000000-0000-0000-0000-000000000006', 'configs.view'),
    ('00000000-0000-0000-0000-000000000006', 'configs.create'),
    ('00000000-0000-0000-0000-000000000006', 'configs.update'),
    ('00000000-0000-0000-0000-000000000006', 'configs.delete');

-- config_operator: view + activate/rollback
INSERT INTO role_permissions (role_id, permission_id) VALUES
    ('00000000-0000-0000-0000-000000000007', 'configs.view'),
    ('00000000-0000-0000-0000-000000000007', 'configs.activate'),
    ('00000000-0000-0000-0000-000000000007', 'configs.rollback');

-- config_admin: всё
INSERT INTO role_permissions (role_id, permission_id) VALUES
    ('00000000-0000-0000-0000-000000000008', 'configs.view'),
    ('00000000-0000-0000-0000-000000000008', 'configs.create'),
    ('00000000-0000-0000-0000-000000000008', 'configs.update'),
    ('00000000-0000-0000-0000-000000000008', 'configs.delete'),
    ('00000000-0000-0000-0000-000000000008', 'configs.activate'),
    ('00000000-0000-0000-0000-000000000008', 'configs.rollback'),
    ('00000000-0000-0000-0000-000000000008', 'configs.schemas.manage'),
    ('00000000-0000-0000-0000-000000000008', 'configs.sensitive.read');

-- ── 4. Перенацеливаем встроенную роль operator (...0003) ────────────────────
-- Раньше: configs.read/write/publish (удалены выше каскадом/явно).
-- Теперь: полный lifecycle конфигов без управления схемами и sensitive.read.
INSERT INTO role_permissions (role_id, permission_id) VALUES
    ('00000000-0000-0000-0000-000000000003', 'configs.view'),
    ('00000000-0000-0000-0000-000000000003', 'configs.create'),
    ('00000000-0000-0000-0000-000000000003', 'configs.update'),
    ('00000000-0000-0000-0000-000000000003', 'configs.delete'),
    ('00000000-0000-0000-0000-000000000003', 'configs.activate'),
    ('00000000-0000-0000-0000-000000000003', 'configs.rollback');
