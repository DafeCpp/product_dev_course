"""Нарезать компактные real-data golden-эпизоды для LOS-31.

Исходные полные логи намеренно остаются локальными и игнорируются Git.
Скрипт запускается из любого каталога и пишет fixtures рядом с собой.
"""

import csv
from dataclasses import dataclass
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO = Path(__file__).resolve().parents[7]


@dataclass(frozen=True)
class Episode:
    output: str
    source: str
    start: int
    stop: int
    scenario: str


EPISODES = (
    Episode(
        "golden_real_tilt_2026_08_02.csv",
        "test_runs/telemetry_log_day_02_08_26.csv",
        26400,
        27300,
        "tilt",
    ),
    Episode(
        "golden_real_failsafe_reconstructed.csv",
        "test_runs/telemetry_log_auto_forward_2.csv",
        0,
        300,
        "failsafe",
    ),
    Episode(
        "golden_real_oversteer_2026_08_02.csv",
        "test_runs/telemetry_log_day_02_08_26.csv",
        19300,
        19650,
        "oversteer",
    ),
    Episode(
        "golden_real_straight_marker.csv",
        "test_runs/telemetry_log_auto_forward_2.csv",
        1214,
        1532,
        "straight",
    ),
    Episode(
        "golden_real_reverse_2026_07_18.csv",
        "test_runs/telemetry_log_forward_back.csv",
        1800,
        2200,
        "reverse",
    ),
)

SOURCE_FIELDS = (
    "ts_ms",
    "ax", "ay", "az",
    "gx", "gy", "gz",
    "mx", "my", "mz",
    "rc_throttle", "rc_steering",
    "pitch_deg", "roll_deg", "heading_deg",
    "slip_deg", "speed_ms", "oversteer_active",
    "test_marker", "drive_mode", "stab_enabled",
)
REPLAY_FIELDS = (
    "mag_present", "rc_present", "wifi_present",
    "wifi_throttle", "wifi_steering", "replay_phase",
)


def _number(row: dict[str, str], key: str) -> float:
    try:
        return float(row.get(key, "") or 0.0)
    except ValueError:
        return 0.0


def _phase(episode: Episode, row: dict[str, str], index: int) -> str:
    if episode.scenario == "oversteer":
        # Внутри большой Drift-вырезки фиксируем один устойчивый эпизод:
        # чистый pre-roll 100:145 и записанное срабатывание 151:181.
        if 100 <= index < 145:
            return "pre"
        if 151 <= index < 181:
            return "assert"
        return "context"
    if episode.scenario == "reverse":
        reverse = _number(row, "rc_throttle") < -0.1
        centered = abs(_number(row, "rc_steering")) < 0.08
        return "assert" if reverse and centered else "context"
    return "assert"


def extract(episode: Episode) -> None:
    source = REPO / episode.source
    with source.open(newline="") as fh:
        reader = csv.DictReader(fh)
        source_fieldnames = set(reader.fieldnames or [])
        rows = list(reader)[episode.start:episode.stop]

    if len(rows) != episode.stop - episode.start:
        raise RuntimeError(f"{source}: expected {episode.stop - episode.start} rows")

    fieldnames = [name for name in SOURCE_FIELDS if name in source_fieldnames]
    fieldnames.extend(REPLAY_FIELDS)

    for index, row in enumerate(rows):
        row["mag_present"] = "1"
        row["rc_present"] = "0" if episode.scenario == "failsafe" else "1"
        row["wifi_present"] = "0"
        row["wifi_throttle"] = "0"
        row["wifi_steering"] = "0"
        row["replay_phase"] = _phase(episode, row, index)

    if episode.scenario == "straight" and not all(
        _number(row, "test_marker") == 1.0 for row in rows
    ):
        raise RuntimeError("straight selector must contain only test_marker=1")
    if episode.scenario == "oversteer" and not any(
        row["replay_phase"] == "assert"
        and row.get("drive_mode") == "2"
        and _number(row, "oversteer_active") > 0.5
        for row in rows
    ):
        raise RuntimeError("oversteer selector lost the recorded Drift event")

    target = HERE / episode.output
    with target.open("w", newline="") as fh:
        writer = csv.DictWriter(
            fh,
            fieldnames=fieldnames,
            extrasaction="ignore",
            lineterminator="\n",
        )
        writer.writeheader()
        writer.writerows(rows)
    print(f"{target.name}: {len(rows)} rows from {episode.source}")


def main() -> None:
    for episode in EPISODES:
        extract(episode)


if __name__ == "__main__":
    main()
