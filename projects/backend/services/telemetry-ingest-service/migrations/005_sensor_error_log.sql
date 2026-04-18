CREATE TABLE sensor_error_log (
    id bigserial PRIMARY KEY,
    sensor_id uuid NOT NULL,
    occurred_at timestamptz NOT NULL DEFAULT now(),
    error_code text NOT NULL,  -- 'rate_limited', 'validation_error', 'unauthorized', 'scope_mismatch', 'not_found', 'internal_error'
    error_message text,
    endpoint text NOT NULL DEFAULT 'rest',  -- 'rest' or 'ws'
    readings_count integer,
    meta jsonb NOT NULL DEFAULT '{}'
);

CREATE INDEX sensor_error_log_sensor_occurred ON sensor_error_log (sensor_id, occurred_at DESC);
CREATE INDEX sensor_error_log_occurred ON sensor_error_log (occurred_at DESC);
