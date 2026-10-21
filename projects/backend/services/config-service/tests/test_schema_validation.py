"""Schema validation must happen even before the first config is created."""
from types import SimpleNamespace
from unittest.mock import AsyncMock

import pytest

from config_service.core.exceptions import ValidationError
from config_service.domain.enums import ConfigType
from config_service.services.schema_service import SchemaService


@pytest.mark.parametrize('schema', [
    {'type': 'not-a-type'},
    {'$schema': 'http://json-schema.org/draft-04/schema#',
     '$ref': '#/default/a b', 'default': {'a b': {'type': 'object'}}},
    {'$schema': 'http://json-schema.org/draft-04/schema#', '$ref': []},
    {'$ref': '#/default', 'default': {'$schema': []}},
    {'$ref': '#/default', 'default': {'$schema': 'https://example.com/unknown'}},
    {'$id': 'not a uri', 'type': 'object'},
    {'$defs': {'child': {'$id': 'not a uri', 'type': 'object'}}},
    {'$id': 'invalid%ZZ'},
    {'$defs': {'child': {'$id': []}}},
    {'$defs': {'child': {'$schema': []}}},
    {'$defs': {'child': {'type': 'not-a-type'}}},
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
    {'$schema': 'https://json-schema.org/draft/2020-12/schema',
     '$ref': 'legacy#/properties/x', '$defs': {'legacy': {
         '$schema': 'http://json-schema.org/draft-04/schema#', 'id': 'legacy',
         'properties': {'x': {'type': 'number', 'minimum': 0, 'exclusiveMinimum': True}}}}},
    {'$schema': 'http://json-schema.org/draft-04/schema#',
     '$ref': '#/default/a%20b', 'default': {'a b': {'type': 'object'}}},
    {'$schema': 'http://json-schema.org/draft-03/schema#', 'definitions': False},
    {'$schema': 'http://json-schema.org/draft-03/schema#',
     'definitions': {'extension': {'type': 'not-a-type'}}},
    {'$schema': 'https://json-schema.org/draft/2020-12/schema',
     '$ref': '#/$defs/legacy', '$defs': {'legacy': {
         '$schema': 'http://json-schema.org/draft-04/schema#',
         'id': 'legacy', 'type': 'number', 'minimum': 0, 'exclusiveMinimum': True}}},
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


@pytest.mark.parametrize('schema,value', [
    ({'$schema': 'http://json-schema.org/draft-07/schema#', '$ref': '#anchor',
      'dependencies': {'a': {'type': 'object'}, 'b': ['x']},
      'definitions': {'x': {'$id': '#anchor', 'type': 'object'}}}, {}),
    ({'$schema': 'https://json-schema.org/draft/2020-12/schema', 'type': 'object',
      'properties': {'amount': {
          '$schema': 'http://json-schema.org/draft-04/schema#',
          'id': 'legacy', 'type': 'number', 'minimum': 0, 'exclusiveMinimum': True}}},
     {'amount': 1}),
])
async def test_activated_schema_is_usable_by_validation_service(schema, value):
    from config_service.services.validation_service import ValidationService

    schema_repo = AsyncMock()
    schema_repo.get_active.return_value = None
    schema_repo.insert_new_version_and_activate.return_value = SimpleNamespace(version=1)
    config_repo = AsyncMock()
    config_repo.list_by_type.return_value = []
    await SchemaService(schema_repo, config_repo).update(ConfigType.feature_flag, schema, 'admin')
    schema_repo.get_active.return_value = SimpleNamespace(schema=schema)
    service = ValidationService(schema_repo)
    await service.validate_strict(ConfigType.feature_flag, value)
    await service.validate_paranoid(ConfigType.feature_flag, value, 'test-config')
    service.invalidate_cache(ConfigType.feature_flag)
    await service.validate_strict(ConfigType.feature_flag, value)
