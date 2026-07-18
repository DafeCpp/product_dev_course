"""Профили физ-модели (FW-S2.9): реестр, JSON round-trip, различие поведения."""

import json
import math
import subprocess
import sys
from dataclasses import fields
from pathlib import Path

import pytest

from simlib import (
    ClosedLoopSim,
    ProfileError,
    Provenance,
    SimParams,
    VehicleModel,
    find_sim_host,
    fitted_params_2026_07_18,
    get_profile,
    get_profile_info,
    list_profile_infos,
    list_profiles,
    load_profile,
    load_profile_info,
    resolve_profile,
    save_profile,
)
from simlib.profiles import CATEGORIES

EXPECTED = {"default", "fitted_2026_07_18", "light", "heavy", "drift"}
_SIM_DIR = Path(__file__).resolve().parent.parent


def _write(tmp_path, payload: dict, name: str = "p.json") -> str:
    """Записать словарь как JSON-профиль во временный файл."""
    path = tmp_path / name
    path.write_text(json.dumps(payload), encoding="utf-8")
    return str(path)


def _valid(**over) -> dict:
    base = {"schema_version": 1, "name": "p",
            "provenance": {"category": "synthetic", "source": "тест"},
            "params": {"mass": 3.0}}
    base.update(over)
    return base


def _drive(params, *, dynamic=False, n=500, dt=0.002, throttle=1.0, steering=0.0):
    """Прогнать модель n шагов и вернуть последний StepOutput."""
    model = VehicleModel(params, dynamic=dynamic)
    out = None
    for _ in range(n):
        out = model.step(dt, throttle, steering)
    return model, out


def _yaw_gain(name: str, throttle: float = 0.5, steering: float = 0.5):
    """|yaw_rate| / кинематический yaw — нормировано по скорости.

    Нормировка обязательна: у `drift` меньше max_accel, и без неё разница в
    поворачиваемости спуталась бы с разницей в скорости.
    """
    params = get_profile(name)
    model = VehicleModel(params, dynamic=True)
    for _ in range(1500):  # разгон до установившейся скорости
        out = model.step(0.002, throttle, 0.0)
    for _ in range(1500):  # руль
        out = model.step(0.002, throttle, steering)
    kinematic = abs(out.speed * math.tan(out.delta_rad) / params.wheelbase_L)
    return abs(out.yaw_rate) / kinematic


# ── Реестр и метаданные ──────────────────────────────────────────────────────

def test_list_profiles_contains_expected():
    assert set(list_profiles()) == EXPECTED
    assert list_profiles() == sorted(list_profiles())


def test_every_shipped_profile_loads_with_valid_metadata():
    for info in list_profile_infos():
        assert info.name in EXPECTED
        assert info.description.strip(), f"{info.name}: пустое описание"
        assert info.provenance.category in CATEGORIES
        assert info.provenance.source.strip(), f"{info.name}: пустой source"
        assert info.schema_version == 1
        for f in fields(SimParams):
            assert math.isfinite(getattr(info.params, f.name))


def test_shipped_profiles_are_complete():
    """Шиппимые профили — полный дамп: не зависят от дефолтов кода."""
    expected = {f.name for f in fields(SimParams)}
    for name in list_profiles():
        raw = json.loads((_SIM_DIR / "simlib" / "data" / "profiles"
                          / f"{name}.json").read_text(encoding="utf-8"))
        assert set(raw["params"]) == expected, f"{name}: неполный набор полей"


def test_registry_returns_independent_copies():
    """Любой путь наружу обязан отдавать копию, иначе мутация отравит кэш.

    `Profile` заморожен, но неглубоко: `params` — мутабельный `SimParams`, так что
    метаданные текут так же, как и сами параметры.
    """
    get_profile("heavy").mass = 99.0
    assert get_profile("heavy").mass == 6.0

    get_profile_info("heavy").params.mass = 98.0
    assert get_profile("heavy").mass == 6.0

    resolve_profile("heavy").params.mass = 97.0
    assert get_profile("heavy").mass == 6.0

    first = list_profiles()[0]
    original = get_profile(first).mass
    list_profile_infos()[0].params.mass = 96.0
    assert get_profile(first).mass == original


def test_default_profile_matches_simparams_defaults():
    """Пин: профиль default == дефолты кода. Упадёт, когда LOS-224 их поменяет."""
    assert get_profile("default") == SimParams()


def test_fitted_wrapper_delegates_to_registry():
    fitted = fitted_params_2026_07_18()
    assert fitted == get_profile("fitted_2026_07_18")
    assert fitted.max_accel == 8.94
    assert fitted.drag_coeff == 1.128
    assert fitted.servo_max_deg == 20.4
    assert fitted.motor_tau == 0.0376
    assert get_profile_info("fitted_2026_07_18").provenance.category == "measured"


# ── Ошибки ───────────────────────────────────────────────────────────────────

def test_unknown_profile_lists_available():
    with pytest.raises(ProfileError) as exc:
        get_profile("nope")
    message = str(exc.value)
    assert "nope" in message
    for name in EXPECTED:
        assert name in message


def test_unknown_param_key_rejected(tmp_path):
    """Опечатка в имени поля обязана падать: молча проигнорированная — худший исход."""
    path = _write(tmp_path, _valid(params={"max_acell": 9.0}))
    with pytest.raises(ProfileError) as exc:
        load_profile(path)
    assert "max_acell" in str(exc.value)
    assert "max_accel" in str(exc.value)  # подсказка


def test_unknown_toplevel_key_rejected(tmp_path):
    with pytest.raises(ProfileError, match="mass"):
        load_profile(_write(tmp_path, _valid(mass=3.0)))


def test_wrong_types_rejected(tmp_path):
    for bad in ("3.0", True, None):
        path = _write(tmp_path, _valid(params={"mass": bad}), name=f"{bad}.json")
        with pytest.raises(ProfileError, match="mass"):
            load_profile(path)
    # NaN: json.load принимает его по умолчанию, профиль — не должен.
    nan_path = tmp_path / "nan.json"
    nan_path.write_text(
        '{"name":"p","provenance":{"category":"synthetic","source":"t"},'
        '"params":{"mass":NaN}}', encoding="utf-8")
    with pytest.raises(ProfileError, match="финитен"):
        load_profile(str(nan_path))


def test_bad_provenance_rejected(tmp_path):
    with pytest.raises(ProfileError, match="category"):
        load_profile(_write(tmp_path, _valid(
            provenance={"category": "guessed", "source": "t"})))
    with pytest.raises(ProfileError, match="source"):
        load_profile(_write(tmp_path, _valid(
            provenance={"category": "synthetic", "source": "  "})))


def test_bad_schema_version_rejected(tmp_path):
    with pytest.raises(ProfileError, match="schema_version"):
        load_profile(_write(tmp_path, _valid(schema_version=2)))


def test_malformed_json_reports_path(tmp_path):
    path = tmp_path / "broken.json"
    path.write_text("{not json", encoding="utf-8")
    with pytest.raises(ProfileError, match="broken.json"):
        load_profile(str(path))


def test_missing_file_reports_path(tmp_path):
    with pytest.raises(ProfileError, match="нет-такого.json"):
        load_profile(str(tmp_path / "нет-такого.json"))


def test_resolve_profile_name_path_and_none(tmp_path, monkeypatch):
    assert resolve_profile(None).name == "default"
    assert resolve_profile("heavy").params.mass == 6.0
    assert resolve_profile(_write(tmp_path, _valid())).name == "p"

    # Имя без разделителей — не путь: ошибка обязана перечислить доступные.
    with pytest.raises(ProfileError) as exc:
        resolve_profile("nope")
    assert "heavy" in str(exc.value)

    # Встроенное имя выигрывает у одноимённого файла в рабочем каталоге.
    (tmp_path / "default.json").write_text(
        json.dumps(_valid(name="default", params={"mass": 42.0})), encoding="utf-8")
    monkeypatch.chdir(tmp_path)
    assert resolve_profile("default").params.mass == 3.0


# ── JSON round-trip ──────────────────────────────────────────────────────────

def test_save_load_round_trip(tmp_path):
    original = get_profile("heavy")
    path = str(tmp_path / "h.json")
    save_profile(original, path)
    assert load_profile(path) == original


def test_save_writes_full_dump(tmp_path):
    """Даже из частичного источника save пишет все поля."""
    partial = load_profile(_write(tmp_path, _valid(params={"mass": 5.0})))
    path = tmp_path / "full.json"
    save_profile(partial, str(path))
    raw = json.loads(path.read_text(encoding="utf-8"))
    assert set(raw["params"]) == {f.name for f in fields(SimParams)}


def test_partial_profile_fills_defaults(tmp_path):
    params = load_profile(_write(tmp_path, _valid(params={"mass": 5.0})))
    assert params.mass == 5.0
    assert params.max_accel == SimParams().max_accel


def test_round_trip_preserves_metadata(tmp_path):
    path = str(tmp_path / "m.json")
    save_profile(get_profile("drift"), path, name="my_drift",
                 description="описание",
                 provenance=Provenance(category="measured", source="источник",
                                       date="2026-07-18", report="r.md"),
                 recommended_dynamic=True)
    info = load_profile_info(path)
    assert info.name == "my_drift"
    assert info.description == "описание"
    assert info.provenance.category == "measured"
    assert info.provenance.date == "2026-07-18"
    assert info.provenance.report == "r.md"
    assert info.recommended_dynamic is True


# ── Различие поведения ───────────────────────────────────────────────────────

def test_profiles_differ_in_speed_transient():
    """Одинаковая v_ss (7.93 м/с), разный транзиент: масса меняет только его.

    `default` намеренно НЕ включён: у него v(1с) ≈ 4.9 — всего ~0.35 м/с от
    fitted, слишком тесно для устойчивого ассерта (у остальных запас > 1 м/с).
    """
    speeds = {}
    for name in ("light", "fitted_2026_07_18", "heavy"):
        params = get_profile(name)
        _, out = _drive(params, n=500)  # 1 с при throttle=1.0
        speeds[name] = out.speed
        assert math.isclose(params.max_accel / params.drag_coeff, 7.93, abs_tol=0.02)

    assert speeds["light"] > speeds["fitted_2026_07_18"] + 0.5
    assert speeds["fitted_2026_07_18"] > speeds["heavy"] + 1.0


def test_all_profiles_produce_distinct_trajectories():
    seen = set()
    for name in list_profiles():
        model, _ = _drive(get_profile(name), dynamic=True, n=800,
                          throttle=0.5, steering=0.3)
        state = model.state
        seen.add((round(state.x, 6), round(state.y, 6), round(state.psi, 6)))
    assert len(seen) == len(list_profiles())


def test_understeer_gradient_sign():
    """K_us = (m·g/L)·(b/Caf − a/Car): < 0 — оверстир, == 0 — нейтральная."""
    def k_us(p):
        return (p.mass * p.gravity_ms2 / p.wheelbase_L) * (
            p.com_b / p.Caf - p.com_a / p.Car)

    assert k_us(get_profile("drift")) < 0
    assert k_us(get_profile("fitted_2026_07_18")) == 0.0


def test_drift_oversteers_with_dynamic():
    """У drift yaw выше кинематического, у нейтрального fitted — равен ему.

    Скорость держится ниже критической (6.0 м/с у drift): выше неё линейная
    модель шин расходится — предел модели, отдельно зафиксирован в описании
    профиля и в PROVENANCE.md.
    """
    drift = _yaw_gain("drift")
    fitted = _yaw_gain("fitted_2026_07_18")
    assert math.isclose(fitted, 1.0, abs_tol=0.05)
    assert drift > 1.25
    assert drift > fitted + 0.25


def test_drift_requires_dynamic():
    """Без dynamic=True шины не участвуют — drift неотличим от копии с шинами fitted."""
    from dataclasses import replace

    drift = get_profile("drift")
    fitted = get_profile("fitted_2026_07_18")
    same_tyres = replace(drift, Caf=fitted.Caf, Car=fitted.Car)

    _, a = _drive(drift, dynamic=False, n=800, throttle=0.5, steering=0.5)
    _, b = _drive(same_tyres, dynamic=False, n=800, throttle=0.5, steering=0.5)
    assert a.yaw_rate == b.yaw_rate


def test_only_drift_recommends_dynamic():
    for info in list_profile_infos():
        assert info.recommended_dynamic == (info.name == "drift")


# ── Интеграция ───────────────────────────────────────────────────────────────

def test_closed_loop_accepts_profile():
    binary = find_sim_host()
    if binary is None:
        pytest.skip("sim_host не собран (см. SIM_HOST_BIN)")
    with ClosedLoopSim(binary, params=get_profile("heavy")) as sim:
        rows = sim.run(200, rc_throttle=0.5, rc_steering=0.0)
    assert rows
    assert math.isfinite(sim.model.state.v)


def test_cli_list_profiles():
    """--list-profiles работает без пути к логу (positional стал опциональным)."""
    result = subprocess.run(
        [sys.executable, "validate_logs.py", "--list-profiles"],
        cwd=_SIM_DIR, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    for name in EXPECTED:
        assert name in result.stdout
