BEGIN;
ALTER TABLE runs ADD COLUMN auto_complete_after_minutes integer;
COMMIT;
