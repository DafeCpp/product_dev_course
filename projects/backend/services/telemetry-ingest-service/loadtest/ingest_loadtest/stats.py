"""Result collection, the run report and SLA evaluation."""
from __future__ import annotations

import math
from collections import Counter
from dataclasses import asdict, dataclass, field
from typing import Any

from ingest_loadtest.scenarios import Scenario, SlaThresholds


def percentile(values: list[float], pct: float) -> float | None:
    """Nearest-rank percentile; ``None`` for an empty sample."""
    if not values:
        return None
    ordered = sorted(values)
    rank = max(1, math.ceil(pct / 100.0 * len(ordered)))
    return ordered[rank - 1]


@dataclass
class Recorder:
    """Per-batch outcomes collected by the runner workers."""

    latencies_ms: list[float] = field(default_factory=list)
    accepted_batches: int = 0
    accepted_readings: int = 0
    rate_limited: int = 0
    errors: Counter[str] = field(default_factory=Counter)
    late_sends: int = 0
    aborted_workers: int = 0

    def accepted(self, latency_ms: float, readings: int) -> None:
        self.accepted_batches += 1
        self.accepted_readings += readings
        self.latencies_ms.append(latency_ms)

    def limited(self) -> None:
        self.rate_limited += 1

    def error(self, kind: str) -> None:
        self.errors[kind] += 1

    def late(self) -> None:
        self.late_sends += 1

    def abort(self) -> None:
        """A sensor worker stopped before finishing its schedule."""
        self.aborted_workers += 1


@dataclass
class Report:
    scenario: str
    transport: str
    sensors: int
    batch_size: int
    elapsed_s: float
    attempted_batches: int
    accepted_batches: int
    accepted_readings: int
    rate_limited: int
    errors: dict[str, int]
    late_sends: int
    aborted_workers: int
    latency_ms: dict[str, float | None]
    throughput_readings_per_s: float
    target_readings_per_s: float | None
    violations: list[str] = field(default_factory=list)

    @property
    def error_count(self) -> int:
        return sum(self.errors.values())

    @property
    def error_rate(self) -> float:
        return self.error_count / self.attempted_batches if self.attempted_batches else 0.0

    @property
    def rate_limited_rate(self) -> float:
        return self.rate_limited / self.attempted_batches if self.attempted_batches else 0.0

    @property
    def passed(self) -> bool:
        return not self.violations

    def to_dict(self) -> dict[str, Any]:
        data = asdict(self)
        data.update(
            error_rate=self.error_rate,
            rate_limited_rate=self.rate_limited_rate,
            passed=self.passed,
        )
        return data


def build_report(scenario: Scenario, recorder: Recorder, elapsed_s: float) -> Report:
    attempted = recorder.accepted_batches + recorder.rate_limited + sum(recorder.errors.values())
    lat = recorder.latencies_ms
    report = Report(
        scenario=scenario.name,
        transport=scenario.transport,
        sensors=scenario.sensors,
        batch_size=scenario.batch_size,
        elapsed_s=round(elapsed_s, 3),
        attempted_batches=attempted,
        accepted_batches=recorder.accepted_batches,
        accepted_readings=recorder.accepted_readings,
        rate_limited=recorder.rate_limited,
        errors=dict(recorder.errors),
        late_sends=recorder.late_sends,
        aborted_workers=recorder.aborted_workers,
        latency_ms={
            "p50": percentile(lat, 50),
            "p95": percentile(lat, 95),
            "p99": percentile(lat, 99),
            "max": max(lat) if lat else None,
        },
        throughput_readings_per_s=recorder.accepted_readings / elapsed_s if elapsed_s > 0 else 0.0,
        target_readings_per_s=scenario.target_readings_per_s(),
    )
    report.violations = evaluate_sla(report, scenario.sla)
    return report


def evaluate_sla(report: Report, sla: SlaThresholds) -> list[str]:
    """Return human-readable violations; an empty list means the run passed."""
    violations: list[str] = []
    if report.attempted_batches == 0:
        return ["no batches were attempted"]
    if report.aborted_workers:
        # The run is shorter than planned, so every other number understates the load.
        violations.append(
            f"{report.aborted_workers} of {report.sensors} sensor workers stopped early"
        )

    def _latency(name: str, limit: float | None) -> None:
        value = report.latency_ms.get(name)
        if limit is None:
            return
        if value is None:
            violations.append(f"{name} latency: no accepted batches to measure")
        elif value > limit:
            violations.append(f"{name} latency {value:.1f} ms > {limit:.1f} ms")

    _latency("p95", sla.max_p95_ms)
    _latency("p99", sla.max_p99_ms)

    if sla.max_error_rate is not None and report.error_rate > sla.max_error_rate:
        violations.append(
            f"error rate {report.error_rate:.4%} > {sla.max_error_rate:.4%} ({report.errors})"
        )
    if sla.max_rate_limited_rate is not None and report.rate_limited_rate > sla.max_rate_limited_rate:
        violations.append(
            f"rate-limited {report.rate_limited_rate:.4%} > {sla.max_rate_limited_rate:.4%}"
        )
    if sla.min_rate_limited_rate is not None and report.rate_limited_rate < sla.min_rate_limited_rate:
        violations.append(
            f"rate-limited {report.rate_limited_rate:.4%} < {sla.min_rate_limited_rate:.4%}: "
            "the limiter did not throttle the load"
        )
    if sla.min_throughput_ratio is not None and report.target_readings_per_s:
        ratio = report.throughput_readings_per_s / report.target_readings_per_s
        if ratio < sla.min_throughput_ratio:
            violations.append(
                f"throughput {report.throughput_readings_per_s:.1f} readings/s is "
                f"{ratio:.1%} of target {report.target_readings_per_s:.1f} "
                f"(< {sla.min_throughput_ratio:.0%})"
            )
    return violations


def format_report(report: Report) -> str:
    lat = report.latency_ms

    def _ms(v: float | None) -> str:
        return "n/a" if v is None else f"{v:.1f}"

    lines = [
        f"scenario={report.scenario} transport={report.transport} sensors={report.sensors} "
        f"batch_size={report.batch_size} elapsed={report.elapsed_s:.1f}s",
        f"batches: attempted={report.attempted_batches} accepted={report.accepted_batches} "
        f"rate_limited={report.rate_limited} errors={report.error_count} late_sends={report.late_sends} "
        f"aborted_workers={report.aborted_workers}",
        f"latency ms: p50={_ms(lat['p50'])} p95={_ms(lat['p95'])} p99={_ms(lat['p99'])} "
        f"max={_ms(lat['max'])}",
        f"throughput: {report.throughput_readings_per_s:.1f} readings/s"
        + (
            f" (target {report.target_readings_per_s:.1f})"
            if report.target_readings_per_s
            else ""
        ),
    ]
    if report.errors:
        lines.append(f"errors by kind: {report.errors}")
    lines.append("SLA: PASS" if report.passed else "SLA: FAIL")
    lines.extend(f"  - {v}" for v in report.violations)
    return "\n".join(lines)
