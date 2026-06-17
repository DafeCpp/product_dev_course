"""Запуск `sim_host` (FW-S2.1) из Python.

`sim_host` — host-исполняемый файл прошивки; сборка через cmake в
`firmware/tests/build/sim_host` (см. FW-S2.1). Здесь — поиск бинаря и batch-прогон.
"""

import os
import subprocess
from pathlib import Path


def find_sim_host() -> str | None:
    """Найти бинарь sim_host.

    Порядок: переменная окружения `SIM_HOST_BIN`, затем дефолтная папка сборки
    `firmware/tests/build/sim_host`. Возвращает путь или None.
    """
    env = os.environ.get("SIM_HOST_BIN")
    if env and Path(env).exists():
        return env
    # simlib → sim → firmware
    firmware = Path(__file__).resolve().parents[2]
    cand = firmware / "tests" / "build" / "sim_host"
    return str(cand) if cand.exists() else None


def run_batch(frames, sim_host_bin: str, identity_calib: bool = True,
              timeout: float = 120.0) -> list[dict]:
    """Прогнать кадры через sim_host в batch-режиме → список выходных строк.

    Каждая выходная строка — dict {имя_колонки: float} по заголовку sim_host.
    """
    args = [sim_host_bin, "--batch"]
    if identity_calib:
        args.append("--identity-calib")
    payload = "".join(f.to_csv() + "\n" for f in frames)
    proc = subprocess.run(args, input=payload, capture_output=True, text=True,
                          timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(f"sim_host exit {proc.returncode}: {proc.stderr}")

    lines = [ln for ln in proc.stdout.splitlines() if ln.strip()]
    if not lines:
        return []
    header = lines[0].split(",")
    out: list[dict] = []
    for ln in lines[1:]:
        vals = ln.split(",")
        out.append({h: float(v) for h, v in zip(header, vals)})
    return out
