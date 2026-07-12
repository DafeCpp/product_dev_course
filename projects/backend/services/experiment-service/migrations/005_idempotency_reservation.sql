-- Make response columns nullable so a pending placeholder can be inserted
-- before the response is known, and add a completion flag.
ALTER TABLE request_idempotency
    ALTER COLUMN response_status DROP NOT NULL,
    ALTER COLUMN response_body DROP NOT NULL,
    ADD COLUMN completed boolean NOT NULL DEFAULT false;

-- All existing rows were already complete.
UPDATE request_idempotency SET completed = true WHERE completed = false;
