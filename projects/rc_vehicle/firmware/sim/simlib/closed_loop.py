"""Closed-loop SIL: Python-модель ↔ sim_host (FW-S2.5).

Замыкает контур через interactive-режим sim_host (FW-S2.1):
  сценарий задаёт RC-команду → sim_host (реальная прошивка) считает applied PWM
  → физ-модель (FW-S2.4) интегрирует шаг → синтезирует сырые сенсоры → обратно.
Детерминированный прогон на фиксированном шаге (по умолчанию 2 мс = 500 Гц),
полный тракт прошивки в контуре (калибровка/Madgwick/EKF).
"""

import subprocess

from .sensors import make_frame
from .sim_params import SimParams
from .vehicle_model import StepOutput, VehicleModel


class ClosedLoopSim:
    def __init__(self, sim_host_bin: str, params: SimParams | None = None,
                 dynamic: bool = False, identity_calib: bool = False,
                 drive_mode: str | None = None,
                 speed_limit: float | None = None,
                 stabilize: bool = False,
                 start_test: str | None = None,
                 target_accel: float | None = None,
                 test_duration: float | None = None,
                 test_steering: float | None = None,
                 wifi_keepalive: bool = False):
        self.p = params or SimParams()
        self.model = VehicleModel(self.p, dynamic=dynamic)
        # StepOutput предыдущего тика → сенсоры текущего кадра (на старте — покой).
        self.last_out = StepOutput(0.0, 0.0, 0.0, 0.0, 0.0)

        args = [sim_host_bin, "--interactive"]
        if identity_calib:
            args.append("--identity-calib")
        if drive_mode:
            args += ["--drive-mode", drive_mode]
        if speed_limit is not None:
            args += ["--speed-limit", repr(float(speed_limit))]
        if stabilize:
            args.append("--stabilize")
        if start_test:
            args += ["--start-test", start_test]
            if target_accel is not None:
                args += ["--target-accel", repr(float(target_accel))]
            if test_duration is not None:
                args += ["--test-duration", repr(float(test_duration))]
            if test_steering is not None:
                args += ["--test-steering", repr(float(test_steering))]
        # Авто-манёвр ведёт машину сам, RC-команд нет → без keepalive сработал
        # бы failsafe и PWM не выдавался бы.
        self.wifi_keepalive = wifi_keepalive or start_test is not None
        self.proc = subprocess.Popen(
            args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, bufsize=1)
        header = self.proc.stdout.readline()
        if not header:
            raise RuntimeError("sim_host: нет заголовка на старте")
        self.header = header.strip().split(",")

    def step(self, dt_ms: int = 2, rc_throttle: float | None = None,
             rc_steering: float | None = None, with_mag: bool = True) -> dict:
        """Один тик контура. rc_*=None → команды нет (failsafe-сценарий).

        Возвращает выход прошивки (dict по колонкам sim_host).
        """
        frame = make_frame(self.last_out, self.model.state.psi, self.p,
                           dt_ms=dt_ms, rc_throttle=rc_throttle,
                           rc_steering=rc_steering, with_mag=with_mag,
                           wifi_keepalive=self.wifi_keepalive)
        self.proc.stdin.write(frame.to_csv() + "\n")
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError("sim_host закрылся неожиданно")
        row = {h: float(v) for h, v in zip(self.header, line.strip().split(","))}
        # Модель интегрирует ПРИМЕНЁННЫЙ выход прошивки → состояние следующего тика.
        self.last_out = self.model.step(dt_ms / 1000.0, row["throttle"],
                                        row["steering"])
        return row

    def run(self, n: int, dt_ms: int = 2, rc_throttle=None, rc_steering=None,
            with_mag: bool = True) -> list[dict]:
        """Прогнать n тиков с постоянной командой (или callable(i)->(thr,str))."""
        rows = []
        for i in range(n):
            if callable(rc_throttle) or callable(rc_steering):
                thr = rc_throttle(i) if callable(rc_throttle) else rc_throttle
                steer = rc_steering(i) if callable(rc_steering) else rc_steering
            else:
                thr, steer = rc_throttle, rc_steering
            rows.append(self.step(dt_ms, thr, steer, with_mag))
        return rows

    def close(self) -> None:
        try:
            if self.proc.stdin:
                self.proc.stdin.close()
        except OSError:
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
