"""Schema validation must happen even before the first config is created."""
from types import SimpleNamespace
from unittest.mock import AsyncMock

import pytest

from config_service.core.exceptions import ValidationError
from config_service.domain.enums import ConfigType
from config_service.services.schema_service import SchemaService


@pytest.mark.parametrize('schema', [
    {'type': 'not-a-type'},
    *[{'$schema': 'http://json-schema.org/draft-03/schema#', keyword: value}
      for keyword in ('type', 'disallow') for value in ('unknown', ['string', 'unknown'])],
    {'$schema': 'http://json-schema.org/draft-03/schema#',
     'properties': {'x': {'type': 'unknown'}}},
    {'$schema': 'http://json-schema.org/draft-03/schema#',
     '$ref': '#/default', 'default': {'disallow': 'unknown'}},
    {'$schema': 'http://json-schema.org/draft-03/schema#', 'type': [{'$ref': '#/missing'}]},
    {'$schema': 'http://json-schema.org/draft-03/schema#', 'disallow': [{'$ref': '#/missing'}]},
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


@pytest.mark.parametrize('keyword', ['type', 'disallow'])
async def test_draft3_schema_entries_work_after_activation(keyword):
    from config_service.core.exceptions import ConfigValidationError
    from config_service.services.validation_service import ValidationService

    schema = {'$schema': 'http://json-schema.org/draft-03/schema#',
              keyword: [{'$ref': '#/default'}], 'default': {'type': 'object'}}
    schema_repo = AsyncMock()
    schema_repo.get_active.return_value = None
    schema_repo.insert_new_version_and_activate.return_value = SimpleNamespace(version=1)
    config_repo = AsyncMock()
    config_repo.list_by_type.return_value = []
    await SchemaService(schema_repo, config_repo).update(ConfigType.feature_flag, schema, 'admin')
    schema_repo.get_active.return_value = SimpleNamespace(schema=schema)
    validator = ValidationService(schema_repo)
    if keyword == 'type':
        await validator.validate_strict(ConfigType.feature_flag, {})
    else:
        with pytest.raises(ConfigValidationError):
            await validator.validate_strict(ConfigType.feature_flag, {})


@pytest.mark.parametrize('keyword', ['type', 'disallow'])
def test_draft3_union_preserves_uniqueness(keyword):
    from config_service.services.schema_validation import build_schema_validator
    draft = {'$schema': 'http://json-schema.org/draft-03/schema#'}
    validator = build_schema_validator({**draft, keyword: [{'type': 'string'}, {'type': 'number'}]})
    assert validator.is_valid('text') == (keyword == 'type')
    assert validator.is_valid(1) == (keyword == 'type')
    with pytest.raises(ValidationError):
        build_schema_validator({**draft, keyword: [{'type': 'string'}, {'type': 'string'}]})


def test_draft3_object_extends():
    from config_service.services.schema_validation import build_schema_validator
    validator = build_schema_validator({'$schema': 'http://json-schema.org/draft-03/schema#',
                                        'extends': {'type': 'string'}})
    assert validator.is_valid('text')
    assert not validator.is_valid(42)


@pytest.mark.parametrize('reference', ['legacy#/properties/x', '#/$defs/legacy/properties/x', 'legacy#target', 'child'])
async def test_reference_inherits_dialect_during_config_validation(reference):
    from config_service.core.exceptions import ConfigValidationError
    from config_service.services.validation_service import ValidationService
    schema = {'$schema': 'https://json-schema.org/draft/2020-12/schema', '$ref': reference,
              '$defs': {'legacy': {'$schema': 'http://json-schema.org/draft-04/schema#',
                                  'id': 'legacy', 'properties': {'x': {
                                      'id': 'child' if reference == 'child' else '#target', 'type': 'object', 'dependencies': {'a': ['b']}}}}}}
    schema_repo = AsyncMock()
    schema_repo.get_active.return_value = None
    config_repo = AsyncMock()
    config_repo.list_by_type.return_value = [SimpleNamespace(id='config', key='key', value={'a': 1})]
    from config_service.core.exceptions import SchemaSanityFailedError
    with pytest.raises(SchemaSanityFailedError):
        await SchemaService(schema_repo, config_repo).update(ConfigType.feature_flag, schema, 'admin')
    schema_repo.insert_new_version_and_activate.assert_not_awaited()
    schema_repo.get_active.return_value = SimpleNamespace(schema=schema)
    service = ValidationService(schema_repo)
    await service.validate_strict(ConfigType.feature_flag, {'a': 1, 'b': 2})
    with pytest.raises(ConfigValidationError):
        await service.validate_strict(ConfigType.feature_flag, {'a': 1})


def test_reference_dialect_annotation_does_not_change_const_data():
    from copy import deepcopy
    from config_service.services.schema_validation import build_schema_validator
    schema = {'const': {'type': 'object'}, 'allOf': [{'$ref': '#/const'}]}
    original = deepcopy(schema)
    validator = build_schema_validator(schema)
    assert validator.is_valid({'type': 'object'})
    assert not validator.is_valid({'type': 'object', '$schema': 'extra'})
    assert schema == original


def test_dynamic_anchor_rebinding_is_preserved():
    from config_service.services.schema_validation import build_schema_validator
    schema = {
        '$id': 'https://example.com/strict', '$dynamicAnchor': 'node', '$ref': 'tree',
        'unevaluatedProperties': False,
        '$defs': {'tree': {
            '$id': 'tree', '$dynamicAnchor': 'node', 'type': 'object',
            'properties': {'data': True, 'children': {
                'type': 'array', 'items': {'$dynamicRef': '#node'}}},
        }},
    }
    validator = build_schema_validator(schema)
    assert validator.is_valid({'children': [{'data': 1}]})
    assert not validator.is_valid({'children': [{'extra': 1}]})


@pytest.mark.parametrize('keyword', ['type', 'disallow', 'extends'])
@pytest.mark.parametrize('intermediate', [False, True])
def test_draft3_pointer_enters_legacy_schema_positions(keyword, intermediate):
    from config_service.services.schema_validation import build_schema_validator
    child = {'id': 'dir/', 'extends': {'$ref': 'target'}}
    if intermediate:
        child = {'properties': {'x': child}}
    union = {keyword: child if keyword == 'extends' else [child]}
    suffix = '' if keyword == 'extends' else '/0'
    if intermediate:
        suffix += '/properties/x'
    schema = {
        '$schema': 'http://json-schema.org/draft-03/schema#',
        'id': 'https://example.com/root',
        '$ref': f'#/definitions/u/{keyword}{suffix}',
        'definitions': {'u': union},
        'properties': {'target': {'id': 'dir/target', 'type': 'string'}},
    }
    validator = build_schema_validator(schema)
    assert validator.is_valid('text')
    assert not validator.is_valid(42)


@pytest.mark.parametrize('keyword', ['type', 'disallow'])
@pytest.mark.parametrize('name', ['any', 'array', 'boolean', 'integer', 'null', 'number', 'object', 'string'])
def test_draft3_known_type_names_remain_supported(keyword, name):
    from config_service.services.schema_validation import build_schema_validator
    validator = build_schema_validator({
        '$schema': 'http://json-schema.org/draft-03/schema#', keyword: name,
    })
    # Exercise runtime type lookup as well as compilation.
    assert isinstance(validator.is_valid(None), bool)
