"""RC vehicle physics model for SIL simulation (FW-S2.4).

Чистый Python (numpy): модель динамики машинки + синтез СЫРЫХ сенсоров в
единицах прошивки (accel g, gyro dps, mag мГс). Кадр совпадает с протоколом
`sim_host` (FW-S2.1) — бинд к exe появится в FW-S2.5.
"""

from .closed_loop import ClosedLoopSim
from .frame import SensorFrame
from .profiles import (
    Profile,
    ProfileError,
    Provenance,
    get_profile,
    get_profile_info,
    list_profile_infos,
    list_profiles,
    load_profile,
    load_profile_info,
    resolve_profile,
    save_profile,
)
from .replay import find_invariant_violations, load_telemetry_csv
from .sensors import (
    RoadExcitation,
    RoadNoiseModel,
    make_frame,
    synth_accel,
    synth_gyro,
    synth_mag,
)
from .sim_host_runner import find_sim_host, run_batch
from .sim_params import (
    SimParams,
    fitted_dynamic_params_2026_07_18,
    fitted_params_2026_07_18,
)
from .validation import (
    SLIP_CHANNELS,
    channel_metrics,
    fit_params,
    fit_params_multi,
    fit_road_noise_params,
    format_report,
    format_road_noise_report,
    load_drive_log,
    load_road_noise_log,
    simulate,
)
from .vehicle_model import StepOutput, VehicleModel, VehicleState

__all__ = [
    "ClosedLoopSim",
    "Profile",
    "ProfileError",
    "Provenance",
    "RoadExcitation",
    "RoadNoiseModel",
    "SLIP_CHANNELS",
    "SensorFrame",
    "SimParams",
    "StepOutput",
    "VehicleModel",
    "VehicleState",
    "channel_metrics",
    "find_invariant_violations",
    "fit_params",
    "fit_params_multi",
    "fit_road_noise_params",
    "fitted_dynamic_params_2026_07_18",
    "fitted_params_2026_07_18",
    "format_report",
    "format_road_noise_report",
    "get_profile",
    "get_profile_info",
    "list_profile_infos",
    "list_profiles",
    "load_drive_log",
    "load_road_noise_log",
    "load_profile",
    "load_profile_info",
    "resolve_profile",
    "save_profile",
    "simulate",
    "find_sim_host",
    "load_telemetry_csv",
    "make_frame",
    "run_batch",
    "synth_accel",
    "synth_gyro",
    "synth_mag",
]
