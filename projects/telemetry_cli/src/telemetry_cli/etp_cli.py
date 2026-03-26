"""etp — CI/CD helper CLI for the Experiment Tracking Platform.

Usage examples:
  etp run create --experiment-id <uuid> --project-id <uuid> --created-by <uuid> --name "CI Run"
  etp run finish --run-id <uuid> --status completed
  etp metrics push --run-id <uuid> --file metrics.json
  etp metrics push --run-id <uuid> --stdin
  etp artifact register --run-id <uuid> --type model --uri s3://bucket/model.pt
"""
from __future__ import annotations

import argparse
import json
import sys
from typing import Any

from telemetry_cli.ci_client import ETPClient, load_metrics_json
from telemetry_cli.ci_config import get_api_url, get_token


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def _require_token() -> str:
    token = get_token()
    if not token:
        print(
            "ERROR: ETP_TOKEN environment variable (or token in ~/.etp/config.toml) is required.",
            file=sys.stderr,
        )
        sys.exit(1)
    return token


def _make_client() -> ETPClient:
    return ETPClient(base_url=get_api_url(), token=_require_token())


# ---------------------------------------------------------------------------
# sub-command handlers
# ---------------------------------------------------------------------------

def _cmd_run_create(args: argparse.Namespace) -> None:
    client = _make_client()

    params: dict[str, Any] | None = None
    if args.params:
        try:
            params = json.loads(args.params)
        except json.JSONDecodeError as exc:
            print(f"ERROR: --params is not valid JSON: {exc}", file=sys.stderr)
            sys.exit(1)

    result = client.run_create(
        experiment_id=args.experiment_id,
        project_id=args.project_id,
        created_by=args.created_by,
        name=args.name,
        params=params,
        notes=args.notes,
        git_sha=args.git_sha,
        env=args.env,
    )
    # Print only the run_id so the shell pipeline can capture it easily.
    print(result["id"])


def _cmd_run_finish(args: argparse.Namespace) -> None:
    client = _make_client()
    result = client.run_finish(run_id=args.run_id, status=args.status)
    print(json.dumps(result, indent=2))


def _cmd_metrics_push(args: argparse.Namespace) -> None:
    if args.stdin and args.file:
        print("ERROR: --stdin and --file are mutually exclusive.", file=sys.stderr)
        sys.exit(1)
    if not args.stdin and not args.file:
        print("ERROR: one of --file or --stdin is required.", file=sys.stderr)
        sys.exit(1)

    if args.stdin:
        raw = sys.stdin.read()
    else:
        try:
            import pathlib
            raw = pathlib.Path(args.file).read_text(encoding="utf-8")
        except OSError as exc:
            print(f"ERROR: cannot read file: {exc}", file=sys.stderr)
            sys.exit(1)

    metrics = load_metrics_json(raw)
    client = _make_client()
    client.metrics_push(run_id=args.run_id, metrics=metrics)
    print(f"OK: pushed {len(metrics)} metric(s) for run {args.run_id}")


def _cmd_artifact_register(args: argparse.Namespace) -> None:
    client = _make_client()
    result = client.artifact_register(
        run_id=args.run_id,
        artifact_type=args.type,
        uri=args.uri,
        checksum=args.checksum,
        size=args.size,
    )
    print(json.dumps(result, indent=2))


# ---------------------------------------------------------------------------
# parser construction
# ---------------------------------------------------------------------------

def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="etp",
        description="Experiment Tracking Platform CI/CD helper",
    )
    sub = parser.add_subparsers(dest="command", metavar="COMMAND")
    sub.required = True

    # ---- run ----
    run_p = sub.add_parser("run", help="Manage experiment runs")
    run_sub = run_p.add_subparsers(dest="subcommand", metavar="SUBCOMMAND")
    run_sub.required = True

    run_create_p = run_sub.add_parser("create", help="Create a new run")
    run_create_p.add_argument("--experiment-id", required=True, metavar="UUID")
    run_create_p.add_argument("--project-id", required=True, metavar="UUID")
    run_create_p.add_argument("--created-by", required=True, metavar="UUID",
                              help="UUID of the user/service account creating the run")
    run_create_p.add_argument("--name", default=None, metavar="TEXT")
    run_create_p.add_argument("--params", default=None, metavar="JSON",
                              help='Hyperparameters as JSON, e.g. \'{"lr": 0.001}\'')
    run_create_p.add_argument("--notes", default=None, metavar="TEXT")
    run_create_p.add_argument("--git-sha", default=None, metavar="SHA")
    run_create_p.add_argument("--env", default=None, metavar="TEXT")
    run_create_p.set_defaults(func=_cmd_run_create)

    run_finish_p = run_sub.add_parser("finish", help="Finish (update status of) a run")
    run_finish_p.add_argument("--run-id", required=True, metavar="UUID")
    run_finish_p.add_argument(
        "--status",
        required=True,
        choices=["running", "succeeded", "failed", "archived"],
        metavar="STATUS",
    )
    run_finish_p.set_defaults(func=_cmd_run_finish)

    # ---- metrics ----
    metrics_p = sub.add_parser("metrics", help="Manage run metrics")
    metrics_sub = metrics_p.add_subparsers(dest="subcommand", metavar="SUBCOMMAND")
    metrics_sub.required = True

    metrics_push_p = metrics_sub.add_parser("push", help="Push metrics from file or stdin")
    metrics_push_p.add_argument("--run-id", required=True, metavar="UUID")
    metrics_push_p.add_argument("--file", default=None, metavar="PATH",
                                help="Path to JSON file with metrics")
    metrics_push_p.add_argument("--stdin", action="store_true",
                                help="Read metrics JSON from stdin")
    metrics_push_p.set_defaults(func=_cmd_metrics_push)

    # ---- artifact ----
    artifact_p = sub.add_parser("artifact", help="Manage run artifacts")
    artifact_sub = artifact_p.add_subparsers(dest="subcommand", metavar="SUBCOMMAND")
    artifact_sub.required = True

    artifact_reg_p = artifact_sub.add_parser("register", help="Register an artifact for a run")
    artifact_reg_p.add_argument("--run-id", required=True, metavar="UUID")
    artifact_reg_p.add_argument("--type", required=True, metavar="TYPE",
                                help='Artifact type, e.g. "model", "dataset", "report"')
    artifact_reg_p.add_argument("--uri", required=True, metavar="URI",
                                help='Storage URI, e.g. "s3://bucket/model.pt"')
    artifact_reg_p.add_argument("--checksum", default=None, metavar="CHECKSUM",
                                help='Optional checksum, e.g. "sha256:abc123"')
    artifact_reg_p.add_argument("--size", default=None, type=int, metavar="BYTES")
    artifact_reg_p.set_defaults(func=_cmd_artifact_register)

    return parser


# ---------------------------------------------------------------------------
# entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = _build_parser()
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
