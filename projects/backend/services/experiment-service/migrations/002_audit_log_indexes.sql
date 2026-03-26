-- 002_audit_log_indexes.sql
-- Add indexes on created_at for audit log tables to support retention cleanup.

BEGIN;

CREATE INDEX IF NOT EXISTS idx_run_events_created_at ON run_events (created_at);
CREATE INDEX IF NOT EXISTS idx_capture_session_events_created_at ON capture_session_events (created_at);

COMMIT;
