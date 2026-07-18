BEGIN;

INSERT INTO config_schemas (config_type, schema, version, is_active, created_by)
VALUES (
    'feature_flag',
    '{
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["enabled"],
        "properties": {
            "enabled": { "type": "boolean" }
        },
        "additionalProperties": false
    }'::jsonb,
    1,
    true,
    'system'
)
ON CONFLICT DO NOTHING;

INSERT INTO config_schemas (config_type, schema, version, is_active, created_by)
VALUES (
    'qos',
    '{
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["__default__"],
        "additionalProperties": { "$ref": "#/$defs/qosSettings" },
        "properties": {
            "__default__": { "$ref": "#/$defs/qosSettings" }
        },
        "$defs": {
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
    1,
    true,
    'system'
)
ON CONFLICT DO NOTHING;

COMMIT;
