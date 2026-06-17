"""RC vehicle physics model for SIL simulation (FW-S2.4).

Чистый Python (numpy): модель динамики машинки + синтез СЫРЫХ сенсоров в
единицах прошивки (accel g, gyro dps, mag мГс). Кадр совпадает с протоколом
`sim_host` (FW-S2.1) — бинд к exe появится в FW-S2.5.
"""

from .frame import SensorFrame
from .sensors import synth_accel, synth_gyro, synth_mag, make_frame
from .sim_params import SimParams
from .vehicle_model import StepOutput, VehicleModel, VehicleState

__all__ = [
    "SensorFrame",
    "SimParams",
    "StepOutput",
    "VehicleModel",
    "VehicleState",
    "make_frame",
    "synth_accel",
    "synth_gyro",
    "synth_mag",
]
