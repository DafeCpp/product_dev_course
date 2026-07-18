"""RC vehicle physics model for SIL simulation (FW-S2.4).

Чистый Python (numpy): модель динамики машинки + синтез СЫРЫХ сенсоров в
единицах прошивки (accel g, gyro dps, mag мГс). Кадр совпадает с протоколом
`sim_host` (FW-S2.1) — бинд к exe появится в FW-S2.5.
"""

from .closed_loop import ClosedLoopSim
from .frame import SensorFrame
from .replay import find_invariant_violations, load_telemetry_csv
from .sensors import synth_accel, synth_gyro, synth_mag, make_frame
from .sim_host_runner import find_sim_host, run_batch
from .sim_params import SimParams, fitted_params_2026_07_18
from .validation import (
    channel_metrics,
    fit_params,
    fit_params_multi,
    format_report,
    load_drive_log,
    simulate,
)
from .vehicle_model import StepOutput, VehicleModel, VehicleState

__all__ = [
    "ClosedLoopSim",
    "SensorFrame",
    "SimParams",
    "StepOutput",
    "VehicleModel",
    "VehicleState",
    "channel_metrics",
    "find_invariant_violations",
    "fit_params",
    "fit_params_multi",
    "fitted_params_2026_07_18",
    "format_report",
    "load_drive_log",
    "simulate",
    "find_sim_host",
    "load_telemetry_csv",
    "make_frame",
    "run_batch",
    "synth_accel",
    "synth_gyro",
    "synth_mag",
]
