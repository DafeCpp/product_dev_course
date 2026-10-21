"""Schema validation must happen even before the first config is created."""
from types import SimpleNamespace
from unittest.mock import AsyncMock

import pytest

from config_service.core.exceptions import ValidationError
from config_service.domain.enums import ConfigType
from config_service.services.schema_service import SchemaService


@pytest.mark.parametrize('schema', [
    {'type': 'not-a-type'},
    {'$schema': []},
    {'required': 42},
    {'properties': {'enabled': 42}},
])
@pytest.mark.parametrize('has_current', [False, True])
async def test_invalid_schema_never_reaches_storage(schema, has_current):
    schema_repo = AsyncMock()
    schema_repo.get_active.return_value = (
        SimpleNamespace(schema={'type': 'object', 'properties': {'enabled': {'type': 'boolean'}}})
        if has_current else None
    )
    config_repo = AsyncMock()
    config_repo.list_by_type.return_value = []
    service = SchemaService(schema_repo, config_repo)

    with pytest.raises(ValidationError, match='Invalid JSON Schema'):
        await service.update(ConfigType.feature_flag, schema, 'admin')

    schema_repo.insert_new_version_and_activate.assert_not_awaited()
    config_repo.list_by_type.assert_not_awaited()


@pytest.mark.parametrize('draft', [
    'http://json-schema.org/draft-07/schema#',
    'https://json-schema.org/draft/2020-12/schema',
])
async def test_valid_first_schema_is_activated(draft):
    schema = {'$schema': draft, 'type': 'object', 'properties': {'enabled': {'type': 'boolean'}}}
    schema_repo = AsyncMock()
    schema_repo.get_active.return_value = None
    schema_repo.insert_new_version_and_activate.return_value = SimpleNamespace(version=1)
    config_repo = AsyncMock()
    config_repo.list_by_type.return_value = []
    await SchemaService(schema_repo, config_repo).update(ConfigType.feature_flag, schema, 'admin')
    schema_repo.insert_new_version_and_activate.assert_awaited_once_with(ConfigType.feature_flag, schema, 'admin')
