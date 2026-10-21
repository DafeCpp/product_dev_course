"""Unit tests for SchemaService.update metaschema validation."""
from __future__ import annotations

from typing import Any
from unittest.mock import AsyncMock

import pytest
from config_service.core.exceptions import InvalidSchemaError
from config_service.domain.enums import ConfigType
from config_service.services.schema_service import SchemaService


def _service() -> tuple[SchemaService, AsyncMock, AsyncMock]:
    schema_repo = AsyncMock()
    schema_repo.get_active.return_value = None
    config_repo = AsyncMock()
    config_repo.list_by_type.return_value = []
    return SchemaService(schema_repo, config_repo), schema_repo, config_repo


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "schema",
    [
        {"type": "object", "properties": {"x": {"type": "not-a-type"}}},
        {"type": "object", "required": "x"},
        # Would crash the compat checker if it ran before the metaschema check.
        {"type": "object", "properties": []},
    ],
)
async def test_update_rejects_invalid_schema_before_insert(schema: dict[str, Any]) -> None:
    svc, schema_repo, _ = _service()

    with pytest.raises(InvalidSchemaError):
        await svc.update(ConfigType.feature_flag, schema, created_by="u1")

    schema_repo.insert_new_version_and_activate.assert_not_called()


@pytest.mark.asyncio
async def test_update_activates_valid_schema_without_configs() -> None:
    svc, schema_repo, _ = _service()
    schema = {"type": "object", "properties": {"x": {"type": "integer"}}}

    await svc.update(ConfigType.feature_flag, schema, created_by="u1")

    schema_repo.insert_new_version_and_activate.assert_awaited_once_with(
        ConfigType.feature_flag, schema, "u1"
    )
