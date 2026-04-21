"""JSON Schema validation for config values."""
from __future__ import annotations

from typing import Any

import jsonschema
import jsonschema.validators
import structlog

from config_service.core.exceptions import ConfigValidationError, SchemaNotFoundError
from config_service.domain.enums import ConfigType
from config_service.prometheus_metrics import config_read_schema_violations_total
from config_service.repositories.schema_repo import SchemaRepository

logger = structlog.get_logger(__name__)


class ValidationService:
    def __init__(self, schema_repo: SchemaRepository) -> None:
        self._schema_repo = schema_repo
        # Cache: config_type -> compiled validator
        self._validators: dict[str, jsonschema.protocols.Validator] = {}

    async def validate_strict(self, config_type: ConfigType, value: dict[str, Any]) -> None:
        """Validate on write — raises ConfigValidationError on failure."""
        validator = await self._get_validator(config_type)
        errors = [e.message for e in validator.iter_errors(value)]
        if errors:
            raise ConfigValidationError(errors)

    async def validate_paranoid(
        self,
        config_type: ConfigType,
        value: dict[str, Any],
        config_id: str,
    ) -> None:
        """Validate on read — logs metric on failure but does not raise."""
        try:
            validator = await self._get_validator(config_type)
        except SchemaNotFoundError:
            return
        errors = [e.message for e in validator.iter_errors(value)]
        if errors:
            config_read_schema_violations_total.labels(
                config_type=config_type.value,
                config_id=config_id,
            ).inc()
            logger.warning(
                "paranoid_schema_violation",
                config_id=config_id,
                config_type=config_type.value,
                errors=errors,
            )

    def invalidate_cache(self, config_type: ConfigType) -> None:
        self._validators.pop(config_type.value, None)

    async def _get_validator(
        self, config_type: ConfigType
    ) -> jsonschema.protocols.Validator:
        cached = self._validators.get(config_type.value)
        if cached is not None:
            return cached

        schema_obj = await self._schema_repo.get_active(config_type)
        if schema_obj is None:
            raise SchemaNotFoundError(config_type.value)

        validator_cls = jsonschema.validators.validator_for(schema_obj.schema)
        validator_cls.check_schema(schema_obj.schema)
        validator = validator_cls(schema_obj.schema)
        self._validators[config_type.value] = validator
        return validator
