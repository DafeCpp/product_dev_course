#!/usr/bin/env python3
"""
Demo seed script — заполняет платформу реалистичными данными.

Создаёт:
  - 2 проекта
  - 4 пользователя (admin уже существует)
  - 5 датчиков с разными типами
  - 3 эксперимента, 6 запусков (разные статусы)
  - capture sessions с телеметрией (~500 точек)
  - метрики запусков (loss, accuracy, force, velocity)
  - артефакты

Использование:
  python3 scripts/seed_demo.py
  python3 scripts/seed_demo.py --base-url http://localhost:8001 --wipe

Требования: requests (pip install requests)
"""
from __future__ import annotations

import argparse
import math
import random
import sys
import time
from datetime import datetime, timezone, timedelta

try:
    import requests
except ImportError:
    print("pip install requests")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

DEFAULT_AUTH   = "http://localhost:8001"
DEFAULT_EXP    = "http://localhost:8002"
DEFAULT_TELEM  = "http://localhost:8003"
DEFAULT_PROXY  = "http://localhost:8080"

ADMIN_USER     = "admin"
ADMIN_PASS     = "Admin123"
BOOTSTRAP_SECRET = "dev-bootstrap-secret"

DEMO_USERS = [
    {"username": "researcher1", "email": "researcher1@demo.local", "password": "Demo1234"},
    {"username": "engineer1",   "email": "engineer1@demo.local",   "password": "Demo1234"},
    {"username": "viewer1",     "email": "viewer1@demo.local",     "password": "Demo1234"},
]

# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------

class Client:
    def __init__(self, base: str, token: str | None = None):
        self.base = base.rstrip("/")
        self.token = token

    def _headers(self, extra: dict | None = None) -> dict:
        h: dict = {"Content-Type": "application/json"}
        if self.token:
            h["Authorization"] = f"Bearer {self.token}"
        if extra:
            h.update(extra)
        return h

    def get(self, path: str, **kw):
        r = requests.get(self.base + path, headers=self._headers(), **kw)
        r.raise_for_status()
        return r.json()

    def post(self, path: str, data: dict | None = None, **kw):
        r = requests.post(self.base + path, json=data or {}, headers=self._headers(), **kw)
        r.raise_for_status()
        return r.json()

    def patch(self, path: str, data: dict):
        r = requests.patch(self.base + path, json=data, headers=self._headers())
        r.raise_for_status()
        return r.json()

    def post_raw(self, path: str, data: dict, extra_headers: dict | None = None):
        r = requests.post(
            self.base + path,
            json=data,
            headers=self._headers(extra_headers),
        )
        r.raise_for_status()
        return r.json()


def login(auth_url: str, username: str, password: str) -> str:
    r = requests.post(
        f"{auth_url}/auth/login",
        json={"username": username, "password": password},
    )
    r.raise_for_status()
    return r.json()["access_token"]


# ---------------------------------------------------------------------------
# Telemetry generators
# ---------------------------------------------------------------------------

def gen_sine(n: int, amp: float, freq: float, noise: float, offset: float = 0.0) -> list[float]:
    rng = random.Random(42)
    return [
        offset + amp * math.sin(2 * math.pi * freq * i / n) + rng.gauss(0, noise)
        for i in range(n)
    ]

def gen_ramp(n: int, start: float, end: float, noise: float) -> list[float]:
    rng = random.Random(7)
    return [
        start + (end - start) * i / max(n - 1, 1) + rng.gauss(0, noise)
        for i in range(n)
    ]

def gen_pulse(n: int, baseline: float, peak: float, duty: float, noise: float) -> list[float]:
    rng = random.Random(13)
    result = []
    for i in range(n):
        phase = (i % int(n * duty)) / (n * duty)
        v = peak if phase < 0.3 else baseline
        result.append(v + rng.gauss(0, noise))
    return result


def send_telemetry(
    telem_url: str,
    sensor_token: str,
    sensor_id: str,
    values: list[float],
    capture_session_id: str | None,
    run_id: str | None,
    start_ts: datetime,
    interval_ms: int = 200,
    signal: str = "value",
) -> int:
    """Send values as batched telemetry. Returns number of readings accepted."""
    BATCH = 50
    accepted = 0
    headers = {"Authorization": f"Bearer {sensor_token}", "Content-Type": "application/json"}
    for i in range(0, len(values), BATCH):
        batch = values[i : i + BATCH]
        readings = [
            {
                "timestamp": (start_ts + timedelta(milliseconds=interval_ms * (i + j))).isoformat(),
                "raw_value": round(v, 4),
                "meta": {"signal": signal},
            }
            for j, v in enumerate(batch)
        ]
        payload: dict = {"sensor_id": sensor_id, "readings": readings}
        if capture_session_id:
            payload["capture_session_id"] = capture_session_id
        if run_id:
            payload["run_id"] = run_id
        r = requests.post(f"{telem_url}/api/v1/telemetry", json=payload, headers=headers)
        if r.ok:
            accepted += r.json().get("accepted", 0)
        else:
            print(f"  ⚠  telemetry batch failed: {r.status_code} {r.text[:80]}")
    return accepted


# ---------------------------------------------------------------------------
# Main seed
# ---------------------------------------------------------------------------

def seed(args: argparse.Namespace) -> None:
    auth_url  = args.auth_url
    exp_url   = args.exp_url
    telem_url = args.telem_url

    print("── bootstrap admin ──────────────────────────────")
    r = requests.post(
        f"{auth_url}/auth/admin/bootstrap",
        json={
            "bootstrap_secret": BOOTSTRAP_SECRET,
            "username": ADMIN_USER,
            "email": "admin@demo.local",
            "password": ADMIN_PASS,
        },
    )
    if r.status_code == 201:
        print("  ✅ admin создан")
    elif r.status_code == 409:
        print("  ℹ  admin уже существует")
    else:
        print(f"  ⚠  bootstrap: {r.status_code}")

    print("── login admin ──────────────────────────────────")
    admin_token = login(auth_url, ADMIN_USER, ADMIN_PASS)
    auth = Client(auth_url, admin_token)
    exp  = Client(exp_url,  admin_token)
    print("  ✅ logged in as admin")

    print("── create demo users ────────────────────────────")
    user_ids: dict[str, str] = {}
    for u in DEMO_USERS:
        try:
            resp = requests.post(
                f"{auth_url}/auth/register",
                json=u,
            )
            if resp.status_code in (200, 201):
                uid = resp.json().get("id") or resp.json().get("user", {}).get("id")
                user_ids[u["username"]] = uid or ""
                print(f"  ✅ {u['username']}")
            elif resp.status_code == 409:
                # already exists — fetch id via admin endpoint
                users_resp = auth.get(f"/auth/admin/users?username={u['username']}")
                items = users_resp.get("users") or users_resp.get("items") or []
                if items:
                    user_ids[u["username"]] = items[0]["id"]
                print(f"  ℹ  {u['username']} уже существует")
            else:
                print(f"  ⚠  {u['username']}: {resp.status_code}")
        except Exception as e:
            print(f"  ⚠  {u['username']}: {e}")

    # ---------------------------------------------------------------------------
    print("── create projects ──────────────────────────────")
    projects: list[dict] = []

    def get_or_create_project(name: str, description: str) -> dict:
        try:
            p = auth.post("/projects", {"name": name, "description": description})
            print(f"  ✅ проект: {name}")
            return p
        except Exception as e:
            print(f"  ⚠  проект {name}: {e}")
            return {}

    p1 = get_or_create_project(
        "Аэродинамическая труба А-12",
        "Испытания аэродинамических профилей и обтекаемых тел в дозвуковой трубе. "
        "Скоростной диапазон 10–120 м/с.",
    )
    p2 = get_or_create_project(
        "RC-автомобиль: динамические испытания",
        "Сбор телеметрии RC-автомобиля на тестовом треке. "
        "IMU, скорость, заряд АКБ.",
    )
    if p1:
        projects.append(p1)
    if p2:
        projects.append(p2)

    if not projects:
        print("❌ Не удалось создать проекты, прерываем")
        sys.exit(1)

    proj1_id = p1.get("id", "")
    proj2_id = p2.get("id", "")

    # ---------------------------------------------------------------------------
    print("── create sensors ───────────────────────────────")
    sensor_tokens: dict[str, str] = {}  # sensor_id → token

    def create_sensor(
        project_id: str,
        name: str,
        stype: str,
        input_unit: str,
        display_unit: str,
        notes: str = "",
    ) -> dict:
        try:
            s = exp.post(
                "/api/v1/sensors",
                {
                    "project_id": project_id,
                    "name": name,
                    "type": stype,
                    "input_unit": input_unit,
                    "display_unit": display_unit,
                    "calibration_notes": notes,
                },
            )
            sid = s["id"]
            # get token via rotate
            tok_resp = exp.post(f"/api/v1/sensors/{sid}/rotate-token", {"project_id": project_id})
            token = tok_resp.get("token", "")
            sensor_tokens[sid] = token
            print(f"  ✅ датчик: {name} ({stype})")
            return s
        except Exception as e:
            print(f"  ⚠  датчик {name}: {e}")
            return {}

    # Wind tunnel sensors
    s_pitot   = create_sensor(proj1_id, "Трубка Пито ПТ-10",    "pitot",       "Pa",  "m/s",  "Калибровка 2026-01")
    s_pstatic = create_sensor(proj1_id, "Давление статическое", "pressure",    "Pa",  "Pa",   "Датчик Honeywell HSCDRNN010NDSA5")
    s_temp    = create_sensor(proj1_id, "Температура потока",   "temperature", "mV",  "°C",   "Термопара тип K")
    # RC car sensors
    s_imu     = create_sensor(proj2_id, "IMU MPU-6050",         "imu",         "raw", "m/s²", "Ось Z — вертикальное ускорение")
    s_speed   = create_sensor(proj2_id, "Энкодер скорости",     "encoder",     "cnt", "km/h", "360 имп/об, колесо D=65мм")

    sensors_p1 = [s for s in [s_pitot, s_pstatic, s_temp] if s]
    sensors_p2 = [s for s in [s_imu, s_speed] if s]

    # ---------------------------------------------------------------------------
    print("── project 1: wind tunnel experiments ──────────")

    # Experiment 1 — succeeded
    ex1 = exp.post(
        "/api/v1/experiments",
        {
            "project_id": proj1_id,
            "name": "Обтекание профиля NACA-0012",
            "description": "Исследование аэродинамических характеристик профиля NACA-0012 "
                           "при различных углах атаки (0°–15°). Re ≈ 3×10⁵.",
            "experiment_type": "wind_tunnel",
            "tags": ["naca0012", "lift", "drag", "2D"],
            "metadata": {"profile": "NACA-0012", "chord": "0.2m", "span": "0.5m"},
        },
    )
    print(f"  ✅ эксперимент: {ex1['name']}")

    # Run 1 — succeeded with telemetry
    run1 = exp.post(
        "/api/v1/runs",
        {
            "experiment_id": ex1["id"],
            "project_id": proj1_id,
            "name": "Угол атаки 0° — базовый",
            "params": {"angle_of_attack": 0, "velocity": 40, "Re": 300000},
            "tags": ["baseline", "aoa_0"],
        },
    )
    # attach sensors
    for s in sensors_p1:
        try:
            exp.post(f"/api/v1/runs/{run1['id']}/sensors", {"sensor_id": s["id"], "project_id": proj1_id})
        except Exception:
            pass
    # start run
    exp.post(f"/api/v1/runs/{run1['id']}/start")
    # capture session
    cs1 = exp.post(
        "/api/v1/capture-sessions",
        {"run_id": run1["id"], "project_id": proj1_id},
    )
    exp.post(f"/api/v1/capture-sessions/{cs1['id']}/start")

    ts_start = datetime.now(timezone.utc) - timedelta(hours=3)
    n = 300
    # pitot: sine (lift curve simulation)
    if s_pitot and s_pitot.get("id") in sensor_tokens:
        vals = gen_sine(n, amp=80, freq=0.5, noise=2, offset=200)
        acc = send_telemetry(
            telem_url, sensor_tokens[s_pitot["id"]], s_pitot["id"],
            vals, cs1["id"], run1["id"], ts_start, signal="velocity",
        )
        print(f"  📡 pitot: {acc} readings")
    # static pressure
    if s_pstatic and s_pstatic.get("id") in sensor_tokens:
        vals = gen_sine(n, amp=15, freq=0.5, noise=0.5, offset=101325)
        acc = send_telemetry(
            telem_url, sensor_tokens[s_pstatic["id"]], s_pstatic["id"],
            vals, cs1["id"], run1["id"], ts_start, signal="pressure",
        )
        print(f"  📡 pressure: {acc} readings")
    # temperature
    if s_temp and s_temp.get("id") in sensor_tokens:
        vals = gen_ramp(n, start=20.0, end=22.5, noise=0.1)
        acc = send_telemetry(
            telem_url, sensor_tokens[s_temp["id"]], s_temp["id"],
            vals, cs1["id"], run1["id"], ts_start, signal="temperature",
        )
        print(f"  📡 temperature: {acc} readings")

    exp.post(f"/api/v1/capture-sessions/{cs1['id']}/stop")
    time.sleep(0.3)
    exp.post(f"/api/v1/runs/{run1['id']}/complete")

    # Run metrics
    for step, (cl, cd) in enumerate(zip(
        gen_sine(20, 0.4, 0.8, 0.01, 0.8),
        gen_ramp(20, 0.02, 0.05, 0.002),
    )):
        try:
            exp.post(
                f"/api/v1/runs/{run1['id']}/metrics",
                {"name": "CL", "step": step, "value": round(cl, 4)},
            )
            exp.post(
                f"/api/v1/runs/{run1['id']}/metrics",
                {"name": "CD", "step": step, "value": round(cd, 5)},
            )
        except Exception:
            pass
    print(f"  ✅ run 1 завершён: {run1['name']}")

    # Run 2 — succeeded, higher AoA
    run2 = exp.post(
        "/api/v1/runs",
        {
            "experiment_id": ex1["id"],
            "project_id": proj1_id,
            "name": "Угол атаки 8°",
            "params": {"angle_of_attack": 8, "velocity": 40, "Re": 300000},
            "tags": ["aoa_8"],
        },
    )
    for s in sensors_p1:
        try:
            exp.post(f"/api/v1/runs/{run2['id']}/sensors", {"sensor_id": s["id"], "project_id": proj1_id})
        except Exception:
            pass
    exp.post(f"/api/v1/runs/{run2['id']}/start")
    cs2 = exp.post("/api/v1/capture-sessions", {"run_id": run2["id"], "project_id": proj1_id})
    exp.post(f"/api/v1/capture-sessions/{cs2['id']}/start")

    ts_start2 = datetime.now(timezone.utc) - timedelta(hours=2)
    if s_pitot and s_pitot.get("id") in sensor_tokens:
        vals = gen_sine(200, amp=100, freq=0.4, noise=3, offset=220)
        acc = send_telemetry(
            telem_url, sensor_tokens[s_pitot["id"]], s_pitot["id"],
            vals, cs2["id"], run2["id"], ts_start2, signal="velocity",
        )
        print(f"  📡 pitot (run2): {acc} readings")

    exp.post(f"/api/v1/capture-sessions/{cs2['id']}/stop")
    time.sleep(0.3)
    exp.post(f"/api/v1/runs/{run2['id']}/complete")

    for step, cl in enumerate(gen_sine(20, 0.55, 0.7, 0.015, 1.1)):
        try:
            exp.post(f"/api/v1/runs/{run2['id']}/metrics", {"name": "CL", "step": step, "value": round(cl, 4)})
        except Exception:
            pass
    print(f"  ✅ run 2 завершён: {run2['name']}")

    # Run 3 — failed (stall)
    run3 = exp.post(
        "/api/v1/runs",
        {
            "experiment_id": ex1["id"],
            "project_id": proj1_id,
            "name": "Угол атаки 18° — срыв",
            "params": {"angle_of_attack": 18, "velocity": 40},
            "tags": ["aoa_18", "stall"],
            "metadata": {"note": "Ожидается срыв потока при ~16°"},
        },
    )
    exp.post(f"/api/v1/runs/{run3['id']}/start")
    exp.post(f"/api/v1/runs/{run3['id']}/fail", {"reason": "Зафиксирован срыв потока, вибрация превысила допустимый уровень"})
    print(f"  ✅ run 3 (failed): {run3['name']}")

    # Experiment 2 — draft
    ex2 = exp.post(
        "/api/v1/experiments",
        {
            "project_id": proj1_id,
            "name": "Вихревые дорожки Кармана за цилиндром",
            "description": "Визуализация и измерение частоты вихреобразования за круговым цилиндром. "
                           "Число Струхаля.",
            "experiment_type": "flow_visualization",
            "tags": ["karman", "cylinder", "strouhal"],
            "metadata": {"cylinder_diameter": "0.05m"},
        },
    )
    print(f"  ✅ эксперимент (draft): {ex2['name']}")

    # ---------------------------------------------------------------------------
    print("── project 2: RC car experiments ────────────────")

    ex3 = exp.post(
        "/api/v1/experiments",
        {
            "project_id": proj2_id,
            "name": "Динамическое торможение: экстренный стоп",
            "description": "Исследование характеристик торможения RC-автомобиля при различных "
                           "скоростях и покрытиях. Измерение пути и замедления.",
            "experiment_type": "dynamics",
            "tags": ["braking", "deceleration", "dynamics"],
        },
    )
    print(f"  ✅ эксперимент: {ex3['name']}")

    run4 = exp.post(
        "/api/v1/runs",
        {
            "experiment_id": ex3["id"],
            "project_id": proj2_id,
            "name": "Асфальт, v₀=40 км/ч",
            "params": {"surface": "asphalt", "initial_speed_kmh": 40, "brake_force": "full"},
            "tags": ["asphalt", "full_brake"],
        },
    )
    for s in sensors_p2:
        try:
            exp.post(f"/api/v1/runs/{run4['id']}/sensors", {"sensor_id": s["id"], "project_id": proj2_id})
        except Exception:
            pass
    exp.post(f"/api/v1/runs/{run4['id']}/start")
    cs4 = exp.post("/api/v1/capture-sessions", {"run_id": run4["id"], "project_id": proj2_id})
    exp.post(f"/api/v1/capture-sessions/{cs4['id']}/start")

    ts_start4 = datetime.now(timezone.utc) - timedelta(hours=1)
    # speed: ramp down (braking)
    if s_speed and s_speed.get("id") in sensor_tokens:
        vals = gen_ramp(150, start=40.0, end=0.0, noise=0.5)
        acc = send_telemetry(
            telem_url, sensor_tokens[s_speed["id"]], s_speed["id"],
            vals, cs4["id"], run4["id"], ts_start4, interval_ms=100, signal="speed",
        )
        print(f"  📡 speed: {acc} readings")
    # imu: deceleration pulse
    if s_imu and s_imu.get("id") in sensor_tokens:
        vals = gen_pulse(150, baseline=0.0, peak=-8.5, duty=0.5, noise=0.3)
        acc = send_telemetry(
            telem_url, sensor_tokens[s_imu["id"]], s_imu["id"],
            vals, cs4["id"], run4["id"], ts_start4, interval_ms=100, signal="accel_z",
        )
        print(f"  📡 imu: {acc} readings")

    exp.post(f"/api/v1/capture-sessions/{cs4['id']}/stop")
    time.sleep(0.3)
    exp.post(f"/api/v1/runs/{run4['id']}/complete")

    for step, decel in enumerate(gen_ramp(15, 8.5, 0.0, 0.1)):
        try:
            exp.post(f"/api/v1/runs/{run4['id']}/metrics", {"name": "deceleration_ms2", "step": step, "value": round(decel, 3)})
        except Exception:
            pass
    print(f"  ✅ run 4 завершён: {run4['name']}")

    # Run 5 — running (active session with live data)
    run5 = exp.post(
        "/api/v1/runs",
        {
            "experiment_id": ex3["id"],
            "project_id": proj2_id,
            "name": "Гравий, v₀=30 км/ч — активный",
            "params": {"surface": "gravel", "initial_speed_kmh": 30},
            "tags": ["gravel", "in_progress"],
        },
    )
    for s in sensors_p2:
        try:
            exp.post(f"/api/v1/runs/{run5['id']}/sensors", {"sensor_id": s["id"], "project_id": proj2_id})
        except Exception:
            pass
    exp.post(f"/api/v1/runs/{run5['id']}/start")
    cs5 = exp.post("/api/v1/capture-sessions", {"run_id": run5["id"], "project_id": proj2_id})
    exp.post(f"/api/v1/capture-sessions/{cs5['id']}/start")

    ts_now = datetime.now(timezone.utc) - timedelta(minutes=2)
    if s_speed and s_speed.get("id") in sensor_tokens:
        vals = gen_ramp(100, start=30.0, end=2.0, noise=1.0)
        send_telemetry(
            telem_url, sensor_tokens[s_speed["id"]], s_speed["id"],
            vals, cs5["id"], run5["id"], ts_now, interval_ms=150, signal="speed",
        )
    if s_imu and s_imu.get("id") in sensor_tokens:
        vals = gen_sine(100, amp=4, freq=1.5, noise=0.8, offset=-0.5)
        send_telemetry(
            telem_url, sensor_tokens[s_imu["id"]], s_imu["id"],
            vals, cs5["id"], run5["id"], ts_now, interval_ms=150, signal="accel_z",
        )
    print(f"  ✅ run 5 (running/active): {run5['name']}")

    # ---------------------------------------------------------------------------
    print("── summary ──────────────────────────────────────")
    print(f"  projects  : {len(projects)}")
    print(f"  sensors   : {len(sensor_tokens)}")
    print("  experiments:")
    print(f"    [{ex1['id'][:8]}…] {ex1['name']} (succeeded)")
    print(f"    [{ex2['id'][:8]}…] {ex2['name']} (draft)")
    print(f"    [{ex3['id'][:8]}…] {ex3['name']} (in progress)")
    print("  runs:")
    for label, r in [
        ("succeeded", run1), ("succeeded", run2), ("failed", run3),
        ("succeeded", run4), ("running",   run5),
    ]:
        print(f"    [{r['id'][:8]}…] {r['name'][:40]:<40} [{label}]")
    print()
    print("✅ Seed завершён.")
    print(f"   Войти: http://localhost:3000  (admin / Admin123)")
    print(f"   Проект 1 id: {proj1_id}")
    print(f"   Проект 2 id: {proj2_id}")


# ---------------------------------------------------------------------------

def wait_service(url: str, name: str, retries: int = 30) -> None:
    for i in range(retries):
        try:
            r = requests.get(url, timeout=3)
            if r.ok:
                print(f"  ✅ {name}")
                return
        except Exception:
            pass
        if i < retries - 1:
            time.sleep(1)
    print(f"  ❌ {name} не отвечает на {url}")
    sys.exit(1)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Seed demo data")
    p.add_argument("--auth-url",  default=DEFAULT_AUTH)
    p.add_argument("--exp-url",   default=DEFAULT_EXP)
    p.add_argument("--telem-url", default=DEFAULT_TELEM)
    p.add_argument("--no-wait",   action="store_true", help="Не ждать готовности сервисов")
    return p.parse_args()


if __name__ == "__main__":
    args = parse_args()

    if not args.no_wait:
        print("── ожидание сервисов ─────────────────────────────")
        wait_service(f"{args.auth_url}/health",  "auth-service")
        wait_service(f"{args.exp_url}/health",   "experiment-service")
        wait_service(f"{args.telem_url}/health", "telemetry-ingest-service")

    seed(args)
