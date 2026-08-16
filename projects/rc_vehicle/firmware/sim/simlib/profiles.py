"""Именованные профили параметров физ-модели (FW-S2.9).

JSON-файлы в `simlib/data/profiles/` — единственный источник истины для встроенных
профилей; Python-констант с числами здесь нет. Профиль = `SimParams` + метаданные
происхождения (измерено / базовая прикидка / расчётная синтетика), см.
`data/profiles/PROVENANCE.md`.

Физические диапазоны (масса > 0 и т.п.) намеренно НЕ проверяются: положительность
уже гарантирует `validation.fit_params`, а range-check заблокировал бы легитимные
эксперименты. Проверяются только структура, типы и финитность.
"""

import difflib
import json
import math
import os
from dataclasses import dataclass, fields, replace
from importlib.resources import files
from typing import Any, Final

from .sim_params import SimParams

SCHEMA_VERSION: Final = 1
CATEGORIES: Final = ("baseline", "measured", "synthetic")

_PARAM_NAMES: Final = frozenset(f.name for f in fields(SimParams))
_ROAD_PARAM_NAMES: Final = frozenset(
    name for name in _PARAM_NAMES if name.startswith("road_"))
_TOP_LEVEL_KEYS: Final = frozenset(
    {"schema_version", "name", "description", "provenance", "recommended_dynamic",
     "params"})
_PROVENANCE_KEYS: Final = frozenset(
    {"category", "source", "date", "firmware", "report"})


class ProfileError(ValueError):
    """Профиль не найден, не читается или не проходит валидацию."""


@dataclass(frozen=True)
class Provenance:
    """Происхождение чисел профиля — чтобы не путать измеренное с придуманным."""

    category: str  # baseline | measured | synthetic
    source: str
    date: str | None = None
    firmware: str | None = None
    report: str | None = None


@dataclass(frozen=True)
class Profile:
    name: str
    description: str
    params: SimParams
    provenance: Provenance
    recommended_dynamic: bool = False
    schema_version: int = SCHEMA_VERSION


def _suggest(key: str, known) -> str:
    """Подсказка «возможно, имелось в виду» — опечатка в ключе иначе молчит."""
    near = difflib.get_close_matches(key, sorted(known), n=1)
    return f"; возможно, имелось в виду: {near[0]}" if near else ""


def _check_keys(data: dict, allowed: frozenset, what: str, origin: str) -> None:
    for key in data:
        if key not in allowed:
            raise ProfileError(
                f"{origin}: неизвестный ключ {what} '{key}'{_suggest(key, allowed)}")


def _number(value: Any, key: str, origin: str) -> float:
    # bool — подкласс int, поэтому проверяется до isinstance.
    if type(value) is bool or not isinstance(value, (int, float)):
        raise ProfileError(
            f"{origin}: параметр '{key}' должен быть числом, получено "
            f"{type(value).__name__} ({value!r})")
    result = float(value)
    if not math.isfinite(result):
        raise ProfileError(f"{origin}: параметр '{key}' не финитен ({value!r})")
    return result


def _parse_provenance(raw: Any, origin: str) -> Provenance:
    if not isinstance(raw, dict):
        raise ProfileError(f"{origin}: 'provenance' должен быть объектом")
    _check_keys(raw, _PROVENANCE_KEYS, "в provenance", origin)
    category = raw.get("category")
    if category not in CATEGORIES:
        raise ProfileError(
            f"{origin}: provenance.category='{category}' — допустимы "
            f"{', '.join(CATEGORIES)}")
    source = raw.get("source")
    if not isinstance(source, str) or not source.strip():
        raise ProfileError(f"{origin}: provenance.source обязателен и непуст")
    return Provenance(
        category=category, source=source, date=raw.get("date"),
        firmware=raw.get("firmware"), report=raw.get("report"))


def parse_profile(data: Any, origin: str) -> Profile:
    """Разобрать словарь JSON → `Profile`. `origin` попадает в тексты ошибок.

    Частичный `params` допускается — недостающие поля берутся из `SimParams()`,
    кроме road_*: отсутствующие road-поля означают clean signal для совместимости
    со schema-1 профилями до LOS-287. Шиппимые профили обязаны быть полными.
    """
    if not isinstance(data, dict):
        raise ProfileError(f"{origin}: ожидался JSON-объект")
    _check_keys(data, _TOP_LEVEL_KEYS, "верхнего уровня", origin)

    version = data.get("schema_version", SCHEMA_VERSION)
    if version != SCHEMA_VERSION:
        raise ProfileError(
            f"{origin}: schema_version={version!r}, поддерживается {SCHEMA_VERSION}")

    name = data.get("name")
    if not isinstance(name, str) or not name.strip():
        raise ProfileError(f"{origin}: поле 'name' обязательно и непусто")

    raw_params = data.get("params", {})
    if not isinstance(raw_params, dict):
        raise ProfileError(f"{origin}: 'params' должен быть объектом")
    _check_keys(raw_params, _PARAM_NAMES, "в params", origin)
    parsed_params = {k: _number(v, k, origin) for k, v in raw_params.items()}
    # До LOS-287 schema-1 профили не знали про road_* и означали чистые
    # сенсоры. Не позволять новым ненулевым SimParams defaults молча менять
    # исторические эксперименты при загрузке старого частичного JSON.
    for key in _ROAD_PARAM_NAMES - raw_params.keys():
        parsed_params[key] = 0.0
    params = SimParams(**parsed_params)

    dynamic = data.get("recommended_dynamic", False)
    if not isinstance(dynamic, bool):
        raise ProfileError(f"{origin}: 'recommended_dynamic' должен быть true/false")

    return Profile(
        name=name,
        description=data.get("description", ""),
        params=params,
        provenance=_parse_provenance(data.get("provenance"), origin),
        recommended_dynamic=dynamic,
        schema_version=version,
    )


def _read_json(path) -> Any:
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)
    except FileNotFoundError as exc:
        raise ProfileError(f"файл профиля не найден: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ProfileError(f"{path}: некорректный JSON ({exc})") from exc


def _builtin_dir():
    return files("simlib") / "data" / "profiles"


_cache: dict[str, Profile] = {}


def _load_builtin(name: str) -> Profile:
    if name not in _cache:
        resource = _builtin_dir() / f"{name}.json"
        data = json.loads(resource.read_text(encoding="utf-8"))
        profile = parse_profile(data, f"профиль '{name}'")
        if profile.name != name:
            raise ProfileError(
                f"профиль '{name}': поле name='{profile.name}' не совпадает с именем файла")
        _cache[name] = profile
    return _cache[name]


def _detached(profile: Profile) -> Profile:
    """Копия профиля, не связанная с кэшем.

    `Profile` заморожен, но заморозка неглубокая: поле `params` — мутабельный
    `SimParams`. Без копии `get_profile_info('heavy').params.mass = 99` отравил бы
    кэш на весь процесс.
    """
    return replace(profile, params=replace(profile.params))


def list_profiles() -> list[str]:
    """Имена встроенных профилей (по алфавиту)."""
    return sorted(p.name[:-len(".json")] for p in _builtin_dir().iterdir()
                  if p.name.endswith(".json"))


def list_profile_infos() -> list[Profile]:
    """Встроенные профили целиком, вместе с метаданными."""
    return [get_profile_info(name) for name in list_profiles()]


def get_profile_info(name: str) -> Profile:
    """Встроенный профиль по имени. Неизвестное имя → `ProfileError` со списком."""
    available = list_profiles()
    if name not in available:
        raise ProfileError(
            f"неизвестный профиль '{name}' — доступны: {', '.join(available)}; "
            f"либо путь к JSON-файлу{_suggest(name, available)}")
    return _detached(_load_builtin(name))


def get_profile(name: str) -> SimParams:
    """Параметры встроенного профиля (свежая копия — `SimParams` мутабелен)."""
    return get_profile_info(name).params


def load_profile_info(path: str | os.PathLike[str]) -> Profile:
    """Прочитать профиль из JSON-файла вместе с метаданными."""
    return parse_profile(_read_json(path), str(path))


def load_profile(path: str | os.PathLike[str]) -> SimParams:
    """Прочитать `SimParams` из JSON-файла профиля."""
    return load_profile_info(path).params


def resolve_profile(spec: str | None) -> Profile:
    """Разрешить CLI-аргумент: имя встроенного профиля, путь к JSON или None.

    Встроенное имя имеет приоритет над файлом — случайный `./default.json` в
    рабочем каталоге не должен перехватывать штатный профиль.
    """
    if spec is None:
        return get_profile_info("default")
    if spec in list_profiles():
        return get_profile_info(spec)  # не _load_builtin: нужна копия, не кэш
    # os.altsep может быть None, а "" in spec истинно всегда — проверять явно,
    # иначе любое имя считается путём и ошибка теряет список доступных профилей.
    looks_like_path = (spec.endswith(".json")
                       or os.sep in spec
                       or (os.altsep is not None and os.altsep in spec)
                       or os.path.exists(spec))
    if looks_like_path:
        return load_profile_info(spec)
    return get_profile_info(spec)  # бросит ProfileError со списком доступных


def save_profile(params: SimParams, path: str | os.PathLike[str], *,
                 name: str | None = None, description: str = "",
                 provenance: Provenance | None = None,
                 recommended_dynamic: bool = False) -> None:
    """Записать `SimParams` в JSON-профиль (всегда полный дамп всех полей).

    Полный дамп — чтобы профиль определял модель независимо от дефолтов кода:
    иначе изменение `SimParams` молча поменяло бы смысл сохранённого файла.
    """
    prov = provenance or Provenance(category="synthetic", source="save_profile()")
    if prov.category not in CATEGORIES:
        raise ProfileError(
            f"provenance.category='{prov.category}' — допустимы {', '.join(CATEGORIES)}")

    prov_json = {"category": prov.category, "source": prov.source}
    for key in ("date", "firmware", "report"):
        value = getattr(prov, key)
        if value is not None:
            prov_json[key] = value

    payload = {
        "schema_version": SCHEMA_VERSION,
        "name": name or os.path.splitext(os.path.basename(str(path)))[0],
        "description": description,
        "provenance": prov_json,
        "recommended_dynamic": recommended_dynamic,
        # Порядок полей — как в объявлении SimParams: читаемые диффы.
        "params": {f.name: getattr(params, f.name) for f in fields(SimParams)},
    }
    with open(path, "w", encoding="utf-8") as fh:
        # allow_nan=False: NaN/Infinity — нестандартный JSON, падать здесь, а не при чтении.
        json.dump(payload, fh, indent=2, ensure_ascii=False, allow_nan=False)
        fh.write("\n")
