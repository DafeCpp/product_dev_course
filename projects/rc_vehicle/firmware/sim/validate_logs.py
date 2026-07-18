"""CLI валидации физ-модели на реальной поездке (FW-S2.6, профили — FW-S2.9).

Прогоняет модель записанными командами лога, подгоняет SimParams, печатает
метрики и (опц.) сохраняет график предсказание-vs-запись.

    python validate_logs.py path/to/telemetry_log.csv --plot out.png
    python validate_logs.py --list-profiles
    python validate_logs.py log.csv --profile heavy --no-fit      # оценить профиль как есть
    python validate_logs.py log.csv --profile drift               # фит со старта профиля
    python validate_logs.py log.csv --save-profile my_chassis.json
"""

import argparse
import datetime
import os
import sys
from dataclasses import replace

from simlib import (
    ProfileError,
    channel_metrics,
    fit_params,
    format_report,
    list_profile_infos,
    resolve_profile,
    save_profile,
    simulate,
)
from simlib.profiles import Provenance
from simlib.validation import load_drive_log


def main() -> None:
    ap = argparse.ArgumentParser()
    # nargs="?" — чтобы --list-profiles работал без пути к логу.
    ap.add_argument("log", nargs="?", help="telemetry_log_*.csv")
    ap.add_argument("--plot", help="путь для PNG-графика")
    ap.add_argument("--profile", metavar="NAME|PATH",
                    help="встроенный профиль или путь к JSON (см. --list-profiles)")
    ap.add_argument("--list-profiles", action="store_true",
                    help="вывести встроенные профили и выйти")
    ap.add_argument("--dynamic", action=argparse.BooleanOptionalAction, default=None,
                    help="динамический велосипед (по умолчанию — из профиля)")
    ap.add_argument("--fit", nargs="*",
                    default=["max_accel", "servo_max_deg", "drag_coeff"],
                    help="поля SimParams для подгонки")
    ap.add_argument("--no-fit", action="store_true", help="без подгонки (как есть)")
    ap.add_argument("--save-profile", metavar="PATH",
                    help="сохранить подогнанные SimParams в JSON-профиль")
    ap.add_argument("--profile-name", help="name для --save-profile")
    args = ap.parse_args()

    if args.list_profiles:
        for info in list_profile_infos():
            print(f"{info.name:20s} {info.provenance.category:10s} {info.description}")
        return
    if not args.log:
        ap.error("нужен путь к логу (или --list-profiles)")

    try:
        prof = resolve_profile(args.profile)
    except ProfileError as exc:  # traceback тут ничего не добавляет
        sys.exit(f"error: {exc}")
    # Явный --dynamic/--no-dynamic перебивает рекомендацию профиля.
    dynamic = args.dynamic if args.dynamic is not None else prof.recommended_dynamic
    print(f"profile: {prof.name} ({prof.provenance.category}), dynamic={dynamic}")

    drive = load_drive_log(args.log)
    base = prof.params
    if args.no_fit:
        fitted, free = base, None
    else:
        fitted, _ = fit_params(drive, base, args.fit, dynamic=dynamic)
        free = args.fit

    pred = simulate(fitted, drive["throttle"], drive["steering"], drive["dt"], dynamic)
    metrics = channel_metrics(pred, drive["rec"])
    print(format_report(metrics, fitted if not args.no_fit else None, free))

    if args.plot:
        from simlib.validation import plot_channels
        plot_channels(pred, drive["rec"], drive["dt"], args.plot)
        print(f"plot → {args.plot}")

    if args.save_profile:
        if args.no_fit:
            # Подгонки не было — на диск идут параметры исходного профиля. Метить
            # их measured нельзя: так синтетика уехала бы в файл как измеренное,
            # ровно та подмена, от которой профили и защищают.
            print("warning: --no-fit — сохраняется копия профиля, не результат фита")
            description = f"копия профиля '{prof.name}' без подгонки"
            provenance = replace(
                prof.provenance,
                source=(f"копия профиля '{prof.name}' (--no-fit, подгонки не было); "
                        f"исходный source: {prof.provenance.source}"))
        else:
            description = f"фит по {os.path.basename(args.log)}"
            provenance = Provenance(
                category="measured",
                source=(f"fit_params({os.path.basename(args.log)}), "
                        f"free={free}, база={prof.name}"),
                date=datetime.date.today().isoformat())
        save_profile(
            fitted, args.save_profile,
            name=args.profile_name,
            description=description,
            provenance=provenance,
            recommended_dynamic=dynamic)
        print(f"profile → {args.save_profile}")


if __name__ == "__main__":
    main()
