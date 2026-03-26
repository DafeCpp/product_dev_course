"""Tests for CI/CD commands: metrics push, run create/finish, artifact register."""
from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Any
from unittest.mock import MagicMock, patch


# ---------------------------------------------------------------------------
# Helpers / fakes
# ---------------------------------------------------------------------------

_RUN_ID = "11111111-1111-1111-1111-111111111111"
_EXPERIMENT_ID = "22222222-2222-2222-2222-222222222222"
_PROJECT_ID = "33333333-3333-3333-3333-333333333333"
_CREATED_BY = "44444444-4444-4444-4444-444444444444"
_ARTIFACT_ID = "55555555-5555-5555-5555-555555555555"


def _fake_response(payload: Any, status_code: int = 200) -> MagicMock:
    resp = MagicMock()
    resp.status_code = status_code
    resp.json.return_value = payload
    resp.raise_for_status.return_value = None
    return resp


# ---------------------------------------------------------------------------
# test_metrics_push_from_file
# ---------------------------------------------------------------------------

class TestMetricsPushFromFile(unittest.TestCase):
    def test_metrics_push_from_file(self) -> None:
        metrics_payload = {
            "metrics": [
                {"name": "accuracy", "step": 1, "value": 0.95},
                {"name": "loss", "step": 1, "value": 0.12},
            ]
        }
        with TemporaryDirectory() as td:
            metrics_file = Path(td) / "metrics.json"
            metrics_file.write_text(json.dumps(metrics_payload), encoding="utf-8")

            with patch("httpx.post") as mock_post, \
                 patch.dict("os.environ", {"ETP_TOKEN": "test-token", "ETP_API_URL": "http://localhost:8002"}):
                mock_post.return_value = _fake_response(None, 202)

                from telemetry_cli.etp_cli import _build_parser
                parser = _build_parser()
                args = parser.parse_args([
                    "metrics", "push",
                    "--run-id", _RUN_ID,
                    "--file", str(metrics_file),
                ])
                args.func(args)

        mock_post.assert_called_once()
        call_kwargs = mock_post.call_args
        assert call_kwargs is not None
        url = call_kwargs.args[0] if call_kwargs.args else call_kwargs.kwargs.get("url", "")
        assert _RUN_ID in url
        body: dict[str, Any] = call_kwargs.kwargs["json"]
        assert len(body["metrics"]) == 2
        assert body["metrics"][0]["name"] == "accuracy"

    def test_metrics_push_from_file_plain_list(self) -> None:
        """Accepts a bare JSON array (without wrapping object)."""
        metrics_payload = [{"name": "loss", "step": 1, "value": 0.3}]
        with TemporaryDirectory() as td:
            metrics_file = Path(td) / "metrics.json"
            metrics_file.write_text(json.dumps(metrics_payload), encoding="utf-8")

            with patch("httpx.post") as mock_post, \
                 patch.dict("os.environ", {"ETP_TOKEN": "tok", "ETP_API_URL": "http://localhost:8002"}):
                mock_post.return_value = _fake_response(None, 202)

                from telemetry_cli.etp_cli import _build_parser
                parser = _build_parser()
                args = parser.parse_args([
                    "metrics", "push",
                    "--run-id", _RUN_ID,
                    "--file", str(metrics_file),
                ])
                args.func(args)

        mock_post.assert_called_once()
        body = mock_post.call_args.kwargs["json"]
        assert len(body["metrics"]) == 1


# ---------------------------------------------------------------------------
# test_metrics_push_from_stdin
# ---------------------------------------------------------------------------

class TestMetricsPushFromStdin(unittest.TestCase):
    def test_metrics_push_stdin(self) -> None:
        raw = json.dumps({"metrics": [{"name": "f1", "step": 5, "value": 0.88}]})

        with patch("httpx.post") as mock_post, \
             patch.dict("os.environ", {"ETP_TOKEN": "tok", "ETP_API_URL": "http://localhost:8002"}), \
             patch("sys.stdin") as mock_stdin:
            mock_stdin.read.return_value = raw
            mock_post.return_value = _fake_response(None, 202)

            from telemetry_cli.etp_cli import _build_parser
            parser = _build_parser()
            args = parser.parse_args(["metrics", "push", "--run-id", _RUN_ID, "--stdin"])
            args.func(args)

        mock_post.assert_called_once()
        body = mock_post.call_args.kwargs["json"]
        assert body["metrics"][0]["name"] == "f1"


# ---------------------------------------------------------------------------
# test_run_create_outputs_id
# ---------------------------------------------------------------------------

class TestRunCreate(unittest.TestCase):
    def test_run_create_outputs_id(self, capsys: Any = None) -> None:
        server_response = {
            "id": _RUN_ID,
            "experiment_id": _EXPERIMENT_ID,
            "status": "draft",
        }
        with patch("httpx.post") as mock_post, \
             patch.dict("os.environ", {"ETP_TOKEN": "tok", "ETP_API_URL": "http://localhost:8002"}):
            mock_post.return_value = _fake_response(server_response, 201)

            from io import StringIO
            captured = StringIO()
            with patch("sys.stdout", captured):
                from telemetry_cli.etp_cli import _build_parser
                parser = _build_parser()
                args = parser.parse_args([
                    "run", "create",
                    "--experiment-id", _EXPERIMENT_ID,
                    "--project-id", _PROJECT_ID,
                    "--created-by", _CREATED_BY,
                    "--name", "CI Run #42",
                    "--params", '{"lr": 0.001}',
                ])
                args.func(args)

        output = captured.getvalue().strip()
        assert output == _RUN_ID

        mock_post.assert_called_once()
        body = mock_post.call_args.kwargs["json"]
        assert body["experiment_id"] == _EXPERIMENT_ID
        assert body["project_id"] == _PROJECT_ID
        assert body["name"] == "CI Run #42"
        assert body["params"] == {"lr": 0.001}

    def test_run_create_without_optional_fields(self) -> None:
        server_response = {"id": _RUN_ID, "experiment_id": _EXPERIMENT_ID, "status": "draft"}
        with patch("httpx.post") as mock_post, \
             patch.dict("os.environ", {"ETP_TOKEN": "tok", "ETP_API_URL": "http://localhost:8002"}):
            mock_post.return_value = _fake_response(server_response, 201)

            from io import StringIO
            with patch("sys.stdout", StringIO()):
                from telemetry_cli.etp_cli import _build_parser
                parser = _build_parser()
                args = parser.parse_args([
                    "run", "create",
                    "--experiment-id", _EXPERIMENT_ID,
                    "--project-id", _PROJECT_ID,
                    "--created-by", _CREATED_BY,
                ])
                args.func(args)

        body = mock_post.call_args.kwargs["json"]
        assert "name" not in body
        assert "params" not in body


# ---------------------------------------------------------------------------
# test_run_finish
# ---------------------------------------------------------------------------

class TestRunFinish(unittest.TestCase):
    def test_run_finish_succeeded(self) -> None:
        server_response = {"id": _RUN_ID, "status": "succeeded"}
        with patch("httpx.patch") as mock_patch, \
             patch.dict("os.environ", {"ETP_TOKEN": "tok", "ETP_API_URL": "http://localhost:8002"}):
            mock_patch.return_value = _fake_response(server_response, 200)

            from telemetry_cli.etp_cli import _build_parser
            parser = _build_parser()
            args = parser.parse_args([
                "run", "finish",
                "--run-id", _RUN_ID,
                "--status", "succeeded",
            ])
            args.func(args)

        mock_patch.assert_called_once()
        url = mock_patch.call_args.args[0]
        assert _RUN_ID in url
        body = mock_patch.call_args.kwargs["json"]
        assert body["status"] == "succeeded"

    def test_run_finish_failed(self) -> None:
        server_response = {"id": _RUN_ID, "status": "failed"}
        with patch("httpx.patch") as mock_patch, \
             patch.dict("os.environ", {"ETP_TOKEN": "tok", "ETP_API_URL": "http://localhost:8002"}):
            mock_patch.return_value = _fake_response(server_response, 200)

            from telemetry_cli.etp_cli import _build_parser
            parser = _build_parser()
            args = parser.parse_args([
                "run", "finish",
                "--run-id", _RUN_ID,
                "--status", "failed",
            ])
            args.func(args)

        body = mock_patch.call_args.kwargs["json"]
        assert body["status"] == "failed"


# ---------------------------------------------------------------------------
# test_artifact_register
# ---------------------------------------------------------------------------

class TestArtifactRegister(unittest.TestCase):
    def test_artifact_register(self) -> None:
        server_response = {
            "id": _ARTIFACT_ID,
            "run_id": _RUN_ID,
            "type": "model",
            "uri": "s3://bucket/model.pt",
            "checksum": "sha256:abc123",
        }
        with patch("httpx.post") as mock_post, \
             patch.dict("os.environ", {"ETP_TOKEN": "tok", "ETP_API_URL": "http://localhost:8002"}):
            mock_post.return_value = _fake_response(server_response, 201)

            from telemetry_cli.etp_cli import _build_parser
            parser = _build_parser()
            args = parser.parse_args([
                "artifact", "register",
                "--run-id", _RUN_ID,
                "--type", "model",
                "--uri", "s3://bucket/model.pt",
                "--checksum", "sha256:abc123",
            ])
            args.func(args)

        mock_post.assert_called_once()
        url = mock_post.call_args.args[0]
        assert _RUN_ID in url
        body = mock_post.call_args.kwargs["json"]
        assert body["type"] == "model"
        assert body["uri"] == "s3://bucket/model.pt"
        assert body["checksum"] == "sha256:abc123"

    def test_artifact_register_without_checksum(self) -> None:
        server_response = {"id": _ARTIFACT_ID, "run_id": _RUN_ID, "type": "dataset", "uri": "gs://b/data.csv"}
        with patch("httpx.post") as mock_post, \
             patch.dict("os.environ", {"ETP_TOKEN": "tok", "ETP_API_URL": "http://localhost:8002"}):
            mock_post.return_value = _fake_response(server_response, 201)

            from telemetry_cli.etp_cli import _build_parser
            parser = _build_parser()
            args = parser.parse_args([
                "artifact", "register",
                "--run-id", _RUN_ID,
                "--type", "dataset",
                "--uri", "gs://b/data.csv",
            ])
            args.func(args)

        body = mock_post.call_args.kwargs["json"]
        assert "checksum" not in body


# ---------------------------------------------------------------------------
# test_missing_token_shows_error
# ---------------------------------------------------------------------------

class TestMissingToken(unittest.TestCase):
    def test_missing_token_shows_error(self) -> None:
        env = {k: v for k, v in __import__("os").environ.items()
               if k not in ("ETP_TOKEN", "ETP_API_URL")}
        # Patch HOME so ~/.etp/config.toml doesn't accidentally exist in test env
        with patch.dict("os.environ", env, clear=True), \
             patch("telemetry_cli.ci_config._CONFIG_PATH",
                   __import__("pathlib").Path("/nonexistent/config.toml")):
            with self.assertRaises(SystemExit) as ctx:
                from telemetry_cli.etp_cli import _build_parser
                parser = _build_parser()
                args = parser.parse_args([
                    "run", "finish",
                    "--run-id", _RUN_ID,
                    "--status", "succeeded",
                ])
                args.func(args)

        assert ctx.exception.code == 1
