"""Schema validation must happen even before the first config is created."""
from types import SimpleNamespace
from unittest.mock import AsyncMock

import pytest

from config_service.core.exceptions import ValidationError
from config_service.domain.enums import ConfigType
from config_service.services.schema_service import SchemaService


@pytest.mark.parametrize('schema', [
    {'type': 'not-a-type'},
    {'$ref': '#/default', 'default': 42},
    {'$ref': '#/default', 'default': {'type': 'invalid'}},
    {'$ref': '#/default', 'default': {'$ref': '#/missing'}},
    {'$id': 'http://[invalid', '$ref': 'child'},
    {'$defs': {'child': {'$id': 'child', '$schema': 'https://example.com/unknown'}}},
    {'$schema': 'http://json-schema.org/draft-07/schema#',
     'definitions': {'child': {'$id': 'child',
         '$schema': 'https://json-schema.org/draft/2020-12/schema',
         '$dynamicRef': '#missing'}}},
    {'$schema': []},
    {'$schema': 'https://example.com/unknown-draft'},
    {'$ref': '#/$defs/missing'},
    {'properties': {'child': {'$ref': '#/missing'}}},
    {'$ref': 'https://example.com/unavailable'},
    {'required': 42},
    {'properties': {'enabled': 42}},
])
@pytest.mark.parametrize('has_current', [False, True])
@pytest.mark.parametrize('has_configs', [False, True])
async def test_invalid_schema_never_reaches_storage(schema, has_current, has_configs):
    schema_repo = AsyncMock()
    schema_repo.get_active.return_value = (
        SimpleNamespace(schema={'type': 'object', 'properties': {'enabled': {'type': 'boolean'}}})
        if has_current else None
    )
    config_repo = AsyncMock()
    config_repo.list_by_type.return_value = [SimpleNamespace(value={})] if has_configs else []
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


@pytest.mark.parametrize('schema', [
    {'$ref': '#/$defs/value', '$defs': {'value': {'type': 'string'}}},
    {'$ref': '#/$defs/a~1b', '$defs': {'a/b': {'type': 'string'}}},
    {'$ref': '#value', '$defs': {'value': {'$anchor': 'value', 'type': 'string'}}},
    {'type': 'object', 'properties': {'child': {'$ref': '#'}}},
    {'$id': 'https://example.com/root', '$ref': 'child',
     '$defs': {'child': {'$id': 'child', 'type': 'string'}}},
    {'default': {'$ref': '#/missing'}, 'const': {'$ref': '#/missing'}},
    {'$schema': 'http://json-schema.org/draft-07/schema#',
     'dependencies': {'a': {'type': 'object'}, 'b': ['x']}},
    {'$schema': 'http://json-schema.org/draft-07/schema#',
     'dependencies': {'b': ['x'], 'a': {'type': 'object'}}},
    {'$ref': '#/default', 'default': {'type': 'string'}},
    {'$ref': '#/default', 'default': True},
    {'$schema': 'http://json-schema.org/draft-07/schema#', '$ref': '#anchor',
     'dependencies': {'a': {'type': 'object'}, 'b': ['x']},
     'definitions': {'x': {'$id': '#anchor', 'type': 'object'}}},
])
async def test_valid_references_and_literal_data_are_accepted(schema):
    schema_repo = AsyncMock()
    schema_repo.get_active.return_value = None
    schema_repo.insert_new_version_and_activate.return_value = SimpleNamespace(version=1)
    config_repo = AsyncMock()
    config_repo.list_by_type.return_value = []
    await SchemaService(schema_repo, config_repo).update(ConfigType.feature_flag, schema, 'admin')
    schema_repo.insert_new_version_and_activate.assert_awaited_once()


async def test_mixed_dependencies_anchor_with_existing_config():
    schema = {'$schema': 'http://json-schema.org/draft-07/schema#', '$ref': '#anchor',
              'dependencies': {'a': {'type': 'object'}, 'b': ['x']},
              'definitions': {'x': {'$id': '#anchor', 'type': 'object'}}}
    schema_repo = AsyncMock()
    schema_repo.get_active.return_value = None
    schema_repo.insert_new_version_and_activate.return_value = SimpleNamespace(version=1)
    config_repo = AsyncMock()
    config_repo.list_by_type.return_value = [SimpleNamespace(value={})]
    await SchemaService(schema_repo, config_repo).update(ConfigType.feature_flag, schema, 'admin')
    schema_repo.insert_new_version_and_activate.assert_awaited_once()
