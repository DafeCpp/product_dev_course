"""Гидравлическая модель канала нагружения.

Зеркало C++ модели (firmware/common/hydraulic_plant_model.cpp):
золотник (звено 1-го порядка) -> расход -> скорость поршня ->
перемещение -> усилие = жёсткость * перемещение. Разрушение образца =
падение жёсткости до остаточной доли.

Согласованность с C++ проверяется тестом test_plant_parity по общему
тест-вектору (см. tests/).
"""

from dataclasses import dataclass, field


@dataclass
class PlantConfig:
    spool_tau_s: float = 0.008
    piston_speed_mm_s: float = 400.0
    stiffness_n_mm: float = 5000.0
    position_limit_mm: float = 80.0


@dataclass
class Plant:
    config: PlantConfig = field(default_factory=PlantConfig)
    spool: float = 0.0
    position_mm: float = 0.0
    force_n: float = 0.0
    stiffness_scale: float = 1.0

    def step(self, valve_cmd: float, dt_s: float) -> None:
        valve_cmd = max(-1.0, min(1.0, valve_cmd))
        if self.config.spool_tau_s > 0:
            alpha = min(dt_s / self.config.spool_tau_s, 1.0)
            self.spool += (valve_cmd - self.spool) * alpha
        else:
            self.spool = valve_cmd
        velocity = self.config.piston_speed_mm_s * self.spool
        self.position_mm = max(
            -self.config.position_limit_mm,
            min(self.config.position_limit_mm,
                self.position_mm + velocity * dt_s))
        self.force_n = (self.config.stiffness_n_mm * self.stiffness_scale *
                        self.position_mm)

    def trigger_failure(self, residual: float = 0.02) -> None:
        self.stiffness_scale = residual


# Масштабы PDO — зеркало bench::PdoScaling (bench_types.hpp)
FULL_SCALE_FORCE_N = 100_000.0
FULL_SCALE_POSITION_MM = 100.0
RAW_FULL_SCALE = 32767.0


def force_to_raw(n: float) -> int:
    return _saturate(n / FULL_SCALE_FORCE_N * RAW_FULL_SCALE)


def position_to_raw(mm: float) -> int:
    return _saturate(mm / FULL_SCALE_POSITION_MM * RAW_FULL_SCALE)


def raw_to_command(raw: int) -> float:
    return raw / RAW_FULL_SCALE


def _saturate(v: float) -> int:
    return int(max(-32767.0, min(32767.0, v)))
