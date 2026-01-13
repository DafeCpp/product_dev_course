BEGIN;

-- Hardening: enqueue deduplication + dispatcher locking + DLQ status.
ALTER TABLE webhook_deliveries
    ADD COLUMN IF NOT EXISTS dedup_key text,
    ADD COLUMN IF NOT EXISTS locked_at timestamptz;

-- Normalize old terminal status name.
UPDATE webhook_deliveries
SET status = 'dead_lettered'
WHERE status = 'failed';

-- Unique dedup key per subscription+event+payload (computed by app). NULLs are allowed.
-- NOTE: must be a real UNIQUE constraint (not a partial index), so we can use ON CONFLICT (dedup_key).
DO $$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM pg_indexes
        WHERE schemaname = current_schema()
          AND indexname = 'webhook_deliveries_dedup_key_uindex'
    ) THEN
        EXECUTE 'DROP INDEX webhook_deliveries_dedup_key_uindex';
    END IF;
EXCEPTION
    WHEN undefined_table THEN
        NULL;
END $$;

DO $$
BEGIN
    ALTER TABLE webhook_deliveries
        ADD CONSTRAINT webhook_deliveries_dedup_key_key UNIQUE (dedup_key);
EXCEPTION
    WHEN duplicate_object OR duplicate_table THEN
        NULL;
END $$;

-- Helpful index for dispatcher claim query.
CREATE INDEX IF NOT EXISTS webhook_deliveries_status_next_attempt_idx
    ON webhook_deliveries (status, next_attempt_at, created_at);

COMMIT;

