"""Unit tests for the load generator in loadtest/ingest_loadtest (no DB, no network)."""
from __future__ import annotations

import asyncio
import json
import time
from collections import Counter
from datetime import datetime, timezone

import pytest

from ingest_loadtest import cli
from ingest_loadtest.provision import load_credentials, save_credentials
from ingest_loadtest.runner import (
    ReadingFactory,
    SensorCredential,
    _accepted_count,
    _StartGate,
    _drive,
    _record_ws_reply,
)
from ingest_loadtest.scenarios import PRESETS, Scenario, SlaThresholds
from ingest_loadtest.stats import Recorder, build_report, evaluate_sla, format_report, percentile
from telemetry_ingest_service.settings import Settings


# --- scenarios ----------------------------------------------------------------


def test_paced_offsets_follow_rate() -> None:
    sc = Scenario(name="t", rate=2.0, duration_s=3.0)
    assert sc.send_offsets() == [0.0, 0.5, 1.0, 1.5, 2.0, 2.5]
    assert sc.batches_per_sensor() == 6


def test_burst_offsets_group_sends_per_period() -> None:
    sc = Scenario(name="t", burst_size=3, burst_every_s=10.0, duration_s=30.0)
    assert sc.send_offsets() == [0.0] * 3 + [10.0] * 3 + [20.0] * 3
    assert sc.batches_per_sensor() == 9


def test_paced_offsets_include_final_partial_interval() -> None:
    sc = Scenario(name="t", rate=2.0, duration_s=1.1)
    assert sc.send_offsets() == [0.0, 0.5, 1.0]
    # Float noise in duration * rate must not add a send at the very end.
    assert Scenario(name="t", rate=10.0, duration_s=0.3).batches_per_sensor() == 3


def test_burst_offsets_include_final_partial_period() -> None:
    sc = Scenario(name="t", burst_size=2, burst_every_s=20.0, duration_s=25.0)
    assert sc.send_offsets() == [0.0, 0.0, 20.0, 20.0]
    assert sc.batches_per_sensor() == 4


def test_unpaced_scenario_has_no_target() -> None:
    sc = Scenario(name="t", rate=0.0)
    assert not sc.paced
    assert sc.batches_per_sensor() is None
    assert sc.target_readings_per_s() is None


def test_target_readings_per_s() -> None:
    sc = Scenario(name="t", sensors=4, rate=0.5, batch_size=10, duration_s=60.0)
    # 30 batches * 4 sensors * 10 readings over 60 s
    assert sc.target_readings_per_s() == pytest.approx(20.0)


@pytest.mark.parametrize(
    "kwargs",
    [
        {"transport": "grpc"},
        {"sensors": 0},
        {"rate": -1.0},
        {"batch_size": 0},
        {"batch_size": 10_001},
        {"duration_s": 0.0},
        {"burst_size": 5, "burst_every_s": 0.0},
    ],
)
def test_scenario_rejects_invalid_values(kwargs: dict) -> None:
    with pytest.raises(ValueError):
        Scenario(name="t", **kwargs)


def test_overrides_ignore_none_and_revalidate() -> None:
    base = PRESETS["baseline"]
    assert base.with_overrides(sensors=None) == base
    assert base.with_overrides(sensors=2).sensors == 2
    with pytest.raises(ValueError):
        base.with_overrides(batch_size=0)


def test_burst_scenario_rejects_rate_override() -> None:
    with pytest.raises(ValueError, match="rate cannot be overridden"):
        PRESETS["burst"].with_overrides(rate=5.0)
    assert PRESETS["burst"].with_overrides(rate=None, sensors=2).sensors == 2


def test_cli_rejects_rate_for_burst(tmp_path, capsys) -> None:
    path = tmp_path / "s.json"
    save_credentials(path, [SensorCredential("a", "t")])
    assert cli.main(["run", "burst", "--sensors-file", str(path), "--rate", "5"]) == 2
    assert "rate cannot be overridden" in capsys.readouterr().err


def _per_window(sc: Scenario, window_s: float) -> tuple[float, float]:
    """Peak batches and readings one sensor sends within any window of ``window_s``."""
    offsets = sc.send_offsets()
    peak = max(sum(1 for o in offsets if start <= o < start + window_s) for start in offsets)
    return peak, peak * sc.batch_size


@pytest.mark.parametrize("name", ["baseline", "sustained"])
def test_steady_presets_stay_under_default_limits(name: str) -> None:
    """Steady presets expect zero 429s, so they must fit the default buckets."""
    sc = PRESETS[name]
    s = Settings()
    if sc.transport == "rest":
        limits = (s.rest_rate_limit_requests_per_window, s.rest_rate_limit_readings_per_window)
        window = s.rest_rate_limit_window_seconds
    else:
        limits = (s.ws_rate_limit_messages_per_window, s.ws_rate_limit_readings_per_window)
        window = s.ws_rate_limit_window_seconds
    batches, readings = _per_window(sc, window)
    assert batches <= limits[0]
    assert readings <= limits[1]


def test_burst_preset_exceeds_default_rest_bucket() -> None:
    """The burst preset exists to exercise the limiter, so one burst must overflow it."""
    sc = PRESETS["burst"]
    assert sc.transport == "rest"
    assert sc.burst_size * sc.batch_size > Settings().rest_rate_limit_readings_per_window


# --- readings -------------------------------------------------------------------


def test_reading_factory_timestamps_are_unique_and_increasing() -> None:
    factory = ReadingFactory(clock=lambda: datetime(2026, 1, 1, tzinfo=timezone.utc))
    stamps = [r["timestamp"] for _ in range(3) for r in factory.batch(4)]
    assert len(set(stamps)) == 12
    assert stamps == sorted(stamps)
    assert stamps[0] == "2026-01-01T00:00:00.000000Z"
    assert stamps[1] == "2026-01-01T00:00:00.000001Z"


def test_reading_factory_follows_the_clock() -> None:
    now = [datetime(2026, 1, 1, tzinfo=timezone.utc)]
    factory = ReadingFactory(clock=lambda: now[0])
    first = factory.batch(2)
    now[0] = datetime(2026, 1, 1, 0, 5, tzinfo=timezone.utc)
    second = factory.batch(2)
    assert first[-1]["timestamp"] == "2026-01-01T00:00:00.000001Z"
    assert second[0]["timestamp"] == "2026-01-01T00:05:00.000000Z"


# --- stats and SLA ------------------------------------------------------------------


def test_percentile_nearest_rank() -> None:
    values = [float(v) for v in range(1, 101)]
    assert percentile(values, 50) == 50.0
    assert percentile(values, 95) == 95.0
    assert percentile(values, 100) == 100.0
    assert percentile([], 95) is None


def _recorder(accepted: int = 0, limited: int = 0, errors: int = 0, latency: float = 10.0) -> Recorder:
    rec = Recorder()
    for _ in range(accepted):
        rec.accepted(latency, 10)
    for _ in range(limited):
        rec.limited()
    for _ in range(errors):
        rec.error("http_500")
    return rec


def test_report_passes_when_within_sla() -> None:
    sc = Scenario(name="t", rate=1.0, batch_size=10, duration_s=10.0, sla=PRESETS["baseline"].sla)
    report = build_report(sc, _recorder(accepted=10), elapsed_s=10.0)
    assert report.passed, report.violations
    assert report.throughput_readings_per_s == pytest.approx(10.0)
    assert "SLA: PASS" in format_report(report)


def test_report_flags_each_violation() -> None:
    sla = SlaThresholds(
        max_p95_ms=50.0,
        max_p99_ms=50.0,
        max_error_rate=0.0,
        max_rate_limited_rate=0.0,
        min_throughput_ratio=0.95,
    )
    sc = Scenario(name="t", rate=1.0, batch_size=10, duration_s=10.0, sla=sla)
    report = build_report(sc, _recorder(accepted=5, limited=2, errors=3, latency=80.0), 10.0)
    text = "\n".join(report.violations)
    for needle in ("p95 latency", "p99 latency", "error rate", "rate-limited", "throughput"):
        assert needle in text
    assert "SLA: FAIL" in format_report(report)
    assert report.to_dict()["passed"] is False


def test_rates_use_attempted_batches() -> None:
    sc = Scenario(name="t", sla=SlaThresholds(max_error_rate=0.25))
    report = build_report(sc, _recorder(accepted=6, limited=2, errors=2), 1.0)
    assert report.attempted_batches == 10
    assert report.error_rate == pytest.approx(0.2)
    assert report.rate_limited_rate == pytest.approx(0.2)
    assert report.passed


def test_latency_check_without_accepted_batches_is_a_violation() -> None:
    sc = Scenario(name="t", sla=SlaThresholds(max_p95_ms=100.0))
    report = build_report(sc, _recorder(errors=1), 1.0)
    assert any("no accepted batches" in v for v in report.violations)


def test_nothing_attempted_fails() -> None:
    report = build_report(Scenario(name="t"), Recorder(), 1.0)
    assert evaluate_sla(report, SlaThresholds()) == ["no batches were attempted"]


# --- credentials file and CLI ---------------------------------------------------


def test_credentials_roundtrip(tmp_path) -> None:
    path = tmp_path / "s.json"
    creds = [SensorCredential("a", "t1"), SensorCredential("b", "t2")]
    save_credentials(path, creds)
    assert load_credentials(path) == creds
    assert path.stat().st_mode & 0o777 == 0o600


def test_credentials_tighten_existing_file(tmp_path) -> None:
    path = tmp_path / "s.json"
    path.write_text("old")
    path.chmod(0o644)
    save_credentials(path, [SensorCredential("a", "t")])
    assert path.stat().st_mode & 0o777 == 0o600
    assert load_credentials(path) == [SensorCredential("a", "t")]


@pytest.mark.parametrize("content", ['{"sensor_id": "a"}', '[{"sensor_id": "a"}]', "[1]"])
def test_credentials_file_validation(tmp_path, content: str) -> None:
    path = tmp_path / "s.json"
    path.write_text(content)
    with pytest.raises(ValueError):
        load_credentials(path)


def test_cli_run_reports_missing_sensors_file(tmp_path, capsys) -> None:
    code = cli.main(["run", "baseline", "--sensors-file", str(tmp_path / "missing.json")])
    assert code == 2
    assert "error:" in capsys.readouterr().err


def test_cli_run_needs_enough_sensors(tmp_path, capsys) -> None:
    path = tmp_path / "s.json"
    save_credentials(path, [SensorCredential("a", "t")])
    assert cli.main(["run", "baseline", "--sensors-file", str(path)]) == 2
    assert "needs 5 sensors" in capsys.readouterr().err


@pytest.mark.parametrize("passed", [True, False])
def test_cli_run_exit_code_and_json(tmp_path, monkeypatch, capsys, passed: bool) -> None:
    path = tmp_path / "s.json"
    save_credentials(path, [SensorCredential("a", "t")])
    seen = {}

    async def fake_run(scenario, creds, base_url, *, request_timeout_s):
        seen.update(scenario=scenario, base_url=base_url, timeout=request_timeout_s)
        rec = _recorder(accepted=1) if passed else _recorder(errors=1)
        return build_report(scenario, rec, 1.0)

    monkeypatch.setattr(cli, "run_scenario", fake_run)
    out = tmp_path / "r.json"
    code = cli.main(
        [
            "run", "ceiling", "--sensors-file", str(path), "--sensors", "1",
            "--transport", "ws", "--duration", "2", "--base-url", "http://x:1",
            "--timeout", "3", "--json-out", str(out),
        ]
    )
    assert code == (0 if passed else 1)
    assert seen["scenario"].transport == "ws"
    assert seen["scenario"].duration_s == 2.0
    assert seen["base_url"] == "http://x:1"
    assert seen["timeout"] == 3.0
    assert json.loads(out.read_text())["passed"] is passed
    assert ("SLA: PASS" if passed else "SLA: FAIL") in capsys.readouterr().out


def test_send_groups_are_lazy_and_grouped() -> None:
    burst = Scenario(name="t", burst_size=3, burst_every_s=1.0, duration_s=2.0)
    assert list(burst.send_groups()) == [(0.0, 3), (1.0, 3)]
    # A huge schedule is generated on demand, not built up front.
    huge = Scenario(name="t", rate=100_000.0, duration_s=300.0)
    groups = huge.send_groups()
    assert next(groups) == (0.0, 1)
    assert next(groups) == (1e-05, 1)


async def test_drive_sends_a_burst_as_one_group() -> None:
    sc = Scenario(name="t", burst_size=3, burst_every_s=0.05, duration_s=0.1, batch_size=2)
    groups: list[int] = []

    async def send(batches) -> None:
        groups.append(len(batches))

    t0 = time.monotonic()
    await _drive(sc, Recorder(), send, t0)
    assert groups == [3, 3]
    assert time.monotonic() - t0 >= sc.duration_s


async def test_paced_drive_waits_out_the_duration() -> None:
    """Throughput is measured over the whole run, not up to the last planned send."""
    sc = Scenario(name="t", rate=10.0, duration_s=0.3)

    async def send(batches) -> None:
        pass

    t0 = time.monotonic()
    await _drive(sc, Recorder(), send, t0)
    assert time.monotonic() - t0 >= sc.duration_s


def test_aborted_workers_fail_the_run() -> None:
    rec = _recorder(accepted=3)
    rec.abort()
    report = build_report(Scenario(name="t", sensors=2), rec, 1.0)
    assert report.violations == ["1 of 2 sensor workers stopped early"]
    assert "aborted_workers=1" in format_report(report)


def test_ceiling_fails_when_throttled() -> None:
    report = build_report(PRESETS["ceiling"], _recorder(accepted=9, limited=1), 1.0)
    assert any("rate-limited" in v for v in report.violations)


def test_burst_fails_without_throttling() -> None:
    sc = PRESETS["burst"]
    unthrottled = build_report(sc, _recorder(accepted=100), 60.0)
    assert any("did not throttle" in v for v in unthrottled.violations)
    throttled = build_report(sc, _recorder(accepted=80, limited=20), 60.0)
    assert throttled.passed, throttled.violations


def test_recorder_counts_errors_by_kind() -> None:
    rec = Recorder()
    rec.error("timeout")
    rec.error("timeout")
    rec.error("http_500")
    assert rec.errors == Counter({"timeout": 2, "http_500": 1})


@pytest.mark.parametrize(
    ("reply", "expected"),
    [
        ({"accepted": 3}, 3),
        ({"accepted": 0}, 0),
        ({"accepted": 10}, 10),
        ({"accepted": 11}, None),
        ({}, None),
        (None, None),
        ({"accepted": True}, None),
        ({"accepted": -1}, None),
    ],
)
def test_accepted_count_uses_service_value(reply, expected: int | None) -> None:
    assert _accepted_count(reply, 10) == expected


def test_ws_reply_classification() -> None:
    rec = Recorder()
    for reply in (None, "ok", [1], {"status": "accepted"}, {"status": "accepted", "accepted": 9}):
        _record_ws_reply(rec, reply, 1.0, 4)
    _record_ws_reply(rec, {"status": "accepted", "accepted": 4}, 2.0, 4)
    _record_ws_reply(rec, {"status": "error", "code": "rate_limited"}, 1.0, 4)
    _record_ws_reply(rec, {"status": "error", "code": "bad_payload"}, 1.0, 4)
    assert rec.errors == Counter({"ws_ack_malformed": 5, "ws_bad_payload": 1})
    assert (rec.accepted_batches, rec.accepted_readings, rec.rate_limited) == (1, 4, 1)


async def test_start_gate_opens_when_every_worker_arrives() -> None:
    gate = _StartGate(2)
    first = asyncio.create_task(gate.arrive())
    await asyncio.sleep(0.05)
    assert not first.done()  # the clock waits for the slow handshake
    t0 = await gate.arrive()
    assert await first == t0 == gate.t0
