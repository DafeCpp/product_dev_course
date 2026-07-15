-- Persist the sensitivity flag on every history snapshot. Existing history
-- cannot be reconstructed reliably, so treat all legacy rows as sensitive.

BEGIN;

ALTER TABLE config_history
    ADD COLUMN is_sensitive BOOLEAN;

UPDATE config_history
SET is_sensitive = true
WHERE is_sensitive IS NULL;

ALTER TABLE config_history
    ALTER COLUMN is_sensitive SET NOT NULL;

COMMIT;
