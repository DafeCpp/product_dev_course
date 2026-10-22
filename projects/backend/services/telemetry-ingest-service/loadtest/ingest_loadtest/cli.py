"""Command line: ``provision`` sensors, ``run`` a scenario, ``cleanup``."""
from __future__ import annotations

import argparse
import asyncio
import json
import sys
from pathlib import Path
from uuid import UUID

from ingest_loadtest.provision import (
    cleanup_sensors,
    load_credentials,
    provision_sensors,
    save_credentials,
)
from ingest_loadtest.runner import run_scenario
from ingest_loadtest.scenarios import PRESETS
from ingest_loadtest.stats import format_report

DEFAULT_DATABASE_URL = "postgresql://experiment_user:experiment_password@localhost:5433/experiment_db"
DEFAULT_BASE_URL = "http://localhost:8003"
DEFAULT_SENSORS_FILE = "loadtest-sensors.json"


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python -m ingest_loadtest",
        description="Load generator for telemetry-ingest-service.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    prov = sub.add_parser("provision", help="create load-test sensors in experiment_db")
    prov.add_argument("--count", type=int, required=True)
    prov.add_argument("--database-url", default=DEFAULT_DATABASE_URL)
    prov.add_argument("--project-id", type=UUID, help="default: a fresh random project id")
    prov.add_argument("--out", type=Path, default=Path(DEFAULT_SENSORS_FILE))

    run = sub.add_parser("run", help="run a scenario and check it against its SLA")
    run.add_argument("scenario", choices=sorted(PRESETS))
    run.add_argument("--base-url", default=DEFAULT_BASE_URL)
    run.add_argument("--sensors-file", type=Path, default=Path(DEFAULT_SENSORS_FILE))
    run.add_argument("--transport", choices=["rest", "ws"])
    run.add_argument("--sensors", type=int)
    run.add_argument("--rate", type=float, help="batches/s per sensor; 0 = back-to-back")
    run.add_argument("--batch-size", type=int)
    run.add_argument("--duration", type=float, dest="duration_s")
    run.add_argument("--timeout", type=float, default=10.0, help="per-request timeout, s")
    run.add_argument("--json-out", type=Path, help="also write the report as JSON")

    clean = sub.add_parser("cleanup", help="delete all load-test sensors and their telemetry")
    clean.add_argument("--database-url", default=DEFAULT_DATABASE_URL)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)

    if args.command == "provision":
        creds = asyncio.run(provision_sensors(args.database_url, args.count, args.project_id))
        save_credentials(args.out, creds)
        print(f"created {len(creds)} sensors, credentials in {args.out}")
        return 0

    if args.command == "cleanup":
        deleted = asyncio.run(cleanup_sensors(args.database_url))
        print(f"deleted {deleted} load-test sensors")
        return 0

    try:
        scenario = PRESETS[args.scenario].with_overrides(
            transport=args.transport,
            sensors=args.sensors,
            rate=args.rate,
            batch_size=args.batch_size,
            duration_s=args.duration_s,
        )
        creds = load_credentials(args.sensors_file)
        report = asyncio.run(
            run_scenario(scenario, creds, args.base_url, request_timeout_s=args.timeout)
        )
    except (ValueError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    print(format_report(report))
    if args.json_out:
        args.json_out.write_text(json.dumps(report.to_dict(), indent=2) + "\n", encoding="utf-8")
    return 0 if report.passed else 1
