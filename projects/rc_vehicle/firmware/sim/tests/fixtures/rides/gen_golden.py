"""Генератор синтетического golden-лога для replay-харнесса (FW-S2.2).

Прогоняет физ-модель (FW-S2.4) по заданному профилю команд и пишет CSV в
ФОРМАТЕ ПРОШИВКИ (тот же заголовок, что у реальных telemetry_log_*.csv).
Заполняются поля, которые читает загрузчик (ts_ms, ax..gz, mx/my/mz, rc_*);
поля-выходы устройства (throttle, ekf_*, …) оставлены нулевыми — при replay их
заново считает sim_host.

Назначение: самодостаточная фикстура для инвариант-тестов без коммита реальных
логов. Реальные golden-вырезки добавляются в FW-S2.3/FW-S2.7.

Запуск: python gen_golden.py golden_synth_drive.csv
"""

import math
import sys

from simlib import VehicleModel, get_profile, make_frame

HEADER = [
    "ts_ms", "ax", "ay", "az", "gx", "gy", "gz", "vx", "vy", "slip_deg",
    "speed_ms", "throttle", "steering", "pitch_deg", "roll_deg", "yaw_deg",
    "yaw_rate_dps", "oversteer_active", "rc_throttle", "rc_steering",
    "cmd_throttle", "cmd_steering", "ekf_vx_var", "ekf_vy_var", "ekf_r_var",
    "ekf_yaw_deg", "mx", "my", "mz", "heading_deg", "heading_rel_deg",
    "test_marker", "event_type", "event_param", "event_value1", "event_value2",
]

DT_S = 0.01  # 100 Гц, как реальный лог
N = 400
GOLDEN_PROFILE = "fitted_2026_07_18"


def main(path: str) -> None:
    # Golden намеренно остаётся clean-signal (LOS-287): параметры road_* этого
    # профиля нулевые, а динамика откалибрована по реальным заездам LOS-32.
    p = get_profile(GOLDEN_PROFILE)
    m = VehicleModel(p)
    rows = []
    ts = 1000
    for i in range(N):
        # Профиль: разгон, затем синусоида руля + лёгкие колебания газа.
        throttle = 0.45 + 0.1 * math.sin(i * 0.02)
        steering = 0.5 * math.sin(i * 0.04)
        out = m.step(DT_S, throttle, steering)
        fr = make_frame(out, m.state.psi, p, dt_ms=int(DT_S * 1000),
                        rc_throttle=throttle, rc_steering=steering)
        row = {c: "" for c in HEADER}
        row.update({
            "ts_ms": ts,
            "ax": fr.ax, "ay": fr.ay, "az": fr.az,
            "gx": fr.gx, "gy": fr.gy, "gz": fr.gz,
            "mx": fr.mx, "my": fr.my, "mz": fr.mz,
            "rc_throttle": throttle, "rc_steering": steering,
            "test_marker": 0,
        })
        rows.append(row)
        ts += int(DT_S * 1000)

    with open(path, "w", newline="") as fh:
        fh.write(",".join(HEADER) + "\n")
        for r in rows:
            fh.write(",".join(str(r[c]) for c in HEADER) + "\n")
    print(f"wrote {len(rows)} rows → {path}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "golden_synth_drive.csv")
