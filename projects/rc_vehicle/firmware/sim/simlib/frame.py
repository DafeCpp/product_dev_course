"""Кадр сырых сенсоров — контракт с протоколом sim_host (FW-S2.1).

Порядок полей `to_csv()` ТОЧНО совпадает с `ParseInputLine` в
tests/sim/stdio_platform.cpp (17 полей). Бинд к exe — в FW-S2.5.
Единицы: accel g, gyro dps, mag мГс.
"""

from dataclasses import dataclass


@dataclass
class SensorFrame:
    dt_ms: int = 2
    ax: float = 0.0
    ay: float = 0.0
    az: float = 1.0  # покой: 1 g по Z
    gx: float = 0.0
    gy: float = 0.0
    gz: float = 0.0
    mag_present: bool = False
    mx: float = 0.0
    my: float = 0.0
    mz: float = 0.0
    rc_present: bool = False
    rc_throttle: float = 0.0
    rc_steering: float = 0.0
    wifi_present: bool = False
    wifi_throttle: float = 0.0
    wifi_steering: float = 0.0

    def to_csv(self) -> str:
        """Строка для stdin sim_host (см. FW-S2.1 ParseInputLine)."""
        def b(x: bool) -> int:
            return 1 if x else 0

        fields = [
            self.dt_ms,
            self.ax, self.ay, self.az,
            self.gx, self.gy, self.gz,
            b(self.mag_present), self.mx, self.my, self.mz,
            b(self.rc_present), self.rc_throttle, self.rc_steering,
            b(self.wifi_present), self.wifi_throttle, self.wifi_steering,
        ]
        return ",".join(repr(v) if isinstance(v, float) else str(v)
                        for v in fields)
