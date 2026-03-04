-- 007_remove_hardcoded_admin.sql
-- Удаляет дефолтного admin-пользователя, созданного в migration 001.
-- Если пароль был уже сменён — пользователь остаётся.
-- Для создания нового admin используйте POST /auth/admin/bootstrap.

BEGIN;

DELETE FROM users
WHERE username = 'admin'
  AND email = 'admin@example.com'
  AND hashed_password = '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG';

COMMIT;
