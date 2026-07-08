-- LOS-101: the seed qos schema (v1, strict __default__/timeout_ms/retries map)
-- rejects the payloads actually written by services and the Rate Limits & QoS
-- UI page: auth_qos (auth-service), experiment_qos (experiment-service) and
-- rate_limits (telemetry-ingest-service). Replace it with an anyOf schema that
-- accepts every known shape; the v1 map shape is kept as a legacy branch.
--
-- On some environments this schema was already applied manually via
-- PUT /api/v1/schemas/qos (curl), hence the NOT EXISTS guard: if an active
-- qos schema other than seed v1 is present, the migration leaves it alone.

BEGIN;

-- Deactivate the seed v1 schema (no-op when already replaced manually).
UPDATE config_schemas
SET is_active = false
WHERE config_type = 'qos' AND version = 1 AND is_active = true;

-- Insert the anyOf schema as the next version, unless an active qos schema
-- already exists (manual curl patch).
INSERT INTO config_schemas (config_type, schema, version, is_active, created_by)
SELECT
    'qos',
    '{
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "anyOf": [
            { "$ref": "#/$defs/authQos" },
            { "$ref": "#/$defs/experimentQos" },
            { "$ref": "#/$defs/telemetryRateLimits" },
            { "$ref": "#/$defs/legacyQosMap" }
        ],
        "$defs": {
            "authQos": {
                "type": "object",
                "minProperties": 1,
                "properties": {
                    "access_token_ttl_sec":  { "type": "integer", "minimum": 1 },
                    "refresh_token_ttl_sec": { "type": "integer", "minimum": 1 }
                },
                "additionalProperties": false
            },
            "experimentQos": {
                "type": "object",
                "minProperties": 1,
                "properties": {
                    "rate_limit_max_requests":    { "type": "integer", "minimum": 1 },
                    "downstream_timeout_seconds": { "type": "number", "exclusiveMinimum": 0 }
                },
                "additionalProperties": false
            },
            "telemetryRateLimits": {
                "type": "object",
                "minProperties": 1,
                "properties": {
                    "rest": { "$ref": "#/$defs/restLimits" },
                    "ws":   { "$ref": "#/$defs/wsLimits" },
                    "spool_flush_timeout_seconds": { "type": "number", "exclusiveMinimum": 0 },
                    "ws_max_message_bytes":        { "type": "integer", "minimum": 1 }
                },
                "additionalProperties": false
            },
            "restLimits": {
                "type": "object",
                "minProperties": 1,
                "properties": {
                    "max_requests":   { "type": "integer", "minimum": 0 },
                    "max_readings":   { "type": "integer", "minimum": 0 },
                    "window_seconds": { "type": "number", "exclusiveMinimum": 0 }
                },
                "additionalProperties": false
            },
            "wsLimits": {
                "type": "object",
                "minProperties": 1,
                "properties": {
                    "max_messages":   { "type": "integer", "minimum": 0 },
                    "max_readings":   { "type": "integer", "minimum": 0 },
                    "window_seconds": { "type": "number", "exclusiveMinimum": 0 }
                },
                "additionalProperties": false
            },
            "legacyQosMap": {
                "type": "object",
                "required": ["__default__"],
                "properties": {
                    "__default__": { "$ref": "#/$defs/qosSettings" }
                },
                "additionalProperties": { "$ref": "#/$defs/qosSettings" }
            },
            "qosSettings": {
                "type": "object",
                "required": ["timeout_ms", "retries"],
                "properties": {
                    "timeout_ms": { "type": "integer", "minimum": 1, "maximum": 600000 },
                    "retries":    { "type": "integer", "minimum": 0, "maximum": 10 }
                },
                "additionalProperties": false
            }
        }
    }'::jsonb,
    COALESCE((SELECT MAX(version) FROM config_schemas WHERE config_type = 'qos'), 0) + 1,
    true,
    'system'
WHERE NOT EXISTS (
    SELECT 1 FROM config_schemas WHERE config_type = 'qos' AND is_active = true
);

COMMIT;
