"""Запуск `sim_host` (FW-S2.1) из Python.

`sim_host` — host-исполняемый файл прошивки; сборка через cmake в
`firmware/tests/build/sim_host` (см. FW-S2.1). Здесь — поиск бинаря и batch-прогон.
"""

import os
import subprocess
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class OversteerReplayConfig:
    """Параметры oversteer guard для воспроизведения записанного конфига."""

    slip_thresh_deg: float = 10.0
    rate_thresh_deg_s: float = 30.0
    throttle_reduction: float = 0.7


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
              timeout: float = 120.0, *, drive_mode: str | None = None,
              stabilize: bool = False,
              oversteer: OversteerReplayConfig | None = None,
              inverted_z_calib: bool = False,
              start_test: str | None = None,
              target_accel: float | None = None,
              test_duration: float | None = None,
              test_steering: float | None = None) -> list[dict]:
    """Прогнать кадры через sim_host в batch-режиме → список выходных строк.

    Каждая выходная строка — dict {имя_колонки: float} по заголовку sim_host.
    """
    args = [sim_host_bin, "--batch"]
    if inverted_z_calib:
        args.append("--inverted-z-calib")
    elif identity_calib:
        args.append("--identity-calib")
    if drive_mode:
        args += ["--drive-mode", drive_mode]
    if stabilize:
        args.append("--stabilize")
    if oversteer is not None:
        args += [
            "--oversteer",
            "--oversteer-slip-thresh", repr(float(oversteer.slip_thresh_deg)),
            "--oversteer-rate-thresh", repr(float(oversteer.rate_thresh_deg_s)),
            "--oversteer-throttle-reduction",
            repr(float(oversteer.throttle_reduction)),
        ]
    if start_test:
        args += ["--start-test", start_test]
        if target_accel is not None:
            args += ["--target-accel", repr(float(target_accel))]
        if test_duration is not None:
            args += ["--test-duration", repr(float(test_duration))]
        if test_steering is not None:
            args += ["--test-steering", repr(float(test_steering))]
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
