"""CLI валидации физ-модели на реальной поездке (FW-S2.6).

Прогоняет модель записанными командами лога, подгоняет SimParams, печатает
метрики и (опц.) сохраняет график предсказание-vs-запись.

    python validate_logs.py path/to/telemetry_log.csv --plot out.png
"""

import argparse

from simlib import SimParams, channel_metrics, fit_params, format_report, simulate
from simlib.validation import load_drive_log


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", help="telemetry_log_*.csv")
    ap.add_argument("--plot", help="путь для PNG-графика")
    ap.add_argument("--dynamic", action="store_true", help="динамический велосипед")
    ap.add_argument("--fit", nargs="*",
                    default=["max_accel", "servo_max_deg", "drag_coeff"],
                    help="поля SimParams для подгонки")
    ap.add_argument("--no-fit", action="store_true", help="без подгонки (дефолты)")
    args = ap.parse_args()

    drive = load_drive_log(args.log)
    base = SimParams()
    if args.no_fit:
        fitted, free = base, None
    else:
        fitted, _ = fit_params(drive, base, args.fit, dynamic=args.dynamic)
        free = args.fit

    pred = simulate(fitted, drive["throttle"], drive["steering"], drive["dt"],
                    args.dynamic)
    metrics = channel_metrics(pred, drive["rec"])
    print(format_report(metrics, fitted if not args.no_fit else None, free))

    if args.plot:
        from simlib.validation import plot_channels
        plot_channels(pred, drive["rec"], drive["dt"], args.plot)
        print(f"plot → {args.plot}")


if __name__ == "__main__":
    main()
