from pathlib import Path

from backend_common.settings.yaml_loader import find_service_yaml, load_service_yaml


def test_finds_and_loads_explicit_yaml_file(tmp_path: Path) -> None:
    service_yaml = tmp_path / "service.yaml"
    service_yaml.write_text("name: example\ndatabase:\n  pool_size: 7\n", encoding="utf-8")

    assert find_service_yaml(tmp_path / "nested") == service_yaml
    assert load_service_yaml(service_yaml) == {"name": "example", "database": {"pool_size": 7}}


def test_missing_or_invalid_explicit_yaml_returns_empty_mapping(tmp_path: Path) -> None:
    assert load_service_yaml(tmp_path / "missing.yaml") == {}
    invalid = tmp_path / "invalid.yaml"
    invalid.write_text("not: [valid", encoding="utf-8")
    assert load_service_yaml(invalid) == {}


def test_implicit_lookup_walks_up_from_current_directory(tmp_path: Path, monkeypatch) -> None:
    service_yaml = tmp_path / "service.yaml"
    service_yaml.write_text("name: from-working-directory\n", encoding="utf-8")
    nested = tmp_path / "one" / "two"
    nested.mkdir(parents=True)
    monkeypatch.chdir(nested)

    assert find_service_yaml() == service_yaml
    assert load_service_yaml() == {"name": "from-working-directory"}


def test_find_service_yaml_stops_when_no_file_exists(tmp_path: Path) -> None:
    assert find_service_yaml(tmp_path) is None
