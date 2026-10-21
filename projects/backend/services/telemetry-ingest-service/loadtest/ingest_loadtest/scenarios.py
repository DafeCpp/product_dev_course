"""Load scenarios and their acceptance thresholds (SLA)."""
from __future__ import annotations

import math
from dataclasses import dataclass, field, replace
from typing import Literal

Transport = Literal["rest", "ws"]


@dataclass(frozen=True)
class SlaThresholds:
    """Acceptance thresholds; ``None`` disables a check.

    Rates are fractions of attempted batches (0.001 = 0.1%). Latency is
    measured on accepted batches only. A run whose sensor workers stopped
    early always fails, whatever the thresholds.
    """

    max_p95_ms: float | None = None
    max_p99_ms: float | None = None
    max_error_rate: float | None = None
    max_rate_limited_rate: float | None = None
    min_rate_limited_rate: float | None = None
    min_throughput_ratio: float | None = None


@dataclass(frozen=True)
class Scenario:
    """How much load to send.

    ``rate`` is batches per second per sensor; ``0`` sends back-to-back
    (one batch in flight per sensor) to find the ceiling. With
    ``burst_size`` set, every ``burst_every_s`` seconds each sensor sends
    ``burst_size`` batches at once and stays idle in between.
    """

    name: str
    transport: Transport = "rest"
    sensors: int = 1
    rate: float = 1.0
    batch_size: int = 10
    duration_s: float = 10.0
    burst_size: int = 0
    burst_every_s: float = 0.0
    sla: SlaThresholds = field(default_factory=SlaThresholds)

    def __post_init__(self) -> None:
        if self.transport not in ("rest", "ws"):
            raise ValueError(f"transport must be 'rest' or 'ws', got {self.transport!r}")
        if self.sensors < 1:
            raise ValueError("sensors must be >= 1")
        if self.rate < 0:
            raise ValueError("rate must be >= 0")
        if not 1 <= self.batch_size <= 10_000:
            raise ValueError("batch_size must be within 1..10000 (ingest API limit)")
        if self.duration_s <= 0:
            raise ValueError("duration_s must be > 0")
        if self.burst_size < 0:
            raise ValueError("burst_size must be >= 0")
        if self.burst_size and self.burst_every_s <= 0:
            raise ValueError("burst_every_s must be > 0 when burst_size is set")

    @property
    def paced(self) -> bool:
        return self.rate > 0 or self.burst_size > 0

    def _burst_periods(self) -> int:
        """Bursts whose start falls inside the run, including a final partial period."""
        # The epsilon keeps an exact multiple (60 / 20) from rounding up to an extra burst.
        return max(1, math.ceil(self.duration_s / self.burst_every_s - 1e-9))

    def batches_per_sensor(self) -> int | None:
        """Planned batches per sensor, or ``None`` for back-to-back mode."""
        if self.burst_size:
            periods = self._burst_periods()
            return periods * self.burst_size
        if self.rate:
            return max(1, int(self.duration_s * self.rate))
        return None

    def target_readings_per_s(self) -> float | None:
        planned = self.batches_per_sensor()
        if planned is None:
            return None
        return planned * self.sensors * self.batch_size / self.duration_s

    def send_offsets(self) -> list[float]:
        """Planned send times (seconds from start) for one sensor."""
        if self.burst_size:
            periods = self._burst_periods()
            return [k * self.burst_every_s for k in range(periods) for _ in range(self.burst_size)]
        planned = self.batches_per_sensor()
        assert planned is not None, "send_offsets() needs a paced scenario"
        return [i / self.rate for i in range(planned)]

    def with_overrides(self, **overrides: object) -> Scenario:
        clean = {k: v for k, v in overrides.items() if v is not None}
        if self.burst_size and "rate" in clean:
            # Bursts take precedence over rate in send_offsets(), so the override would be ignored.
            raise ValueError(f"scenario {self.name!r} sends bursts; rate cannot be overridden")
        return replace(self, **clean)  # type: ignore[arg-type]


# Default per-sensor limits (settings.py): REST 600 requests / 60 000 readings
# per 60 s, WS 600 messages / 60 000 readings per 1 s. Presets stay under them
# except ``burst``, which exists to exercise the limiter: each burst carries
# 75 000 readings, above the 60 000 bucket.

_STEADY_SLA = SlaThresholds(
    max_p95_ms=200.0,
    max_p99_ms=500.0,
    max_error_rate=0.001,
    max_rate_limited_rate=0.0,
    min_throughput_ratio=0.95,
)

PRESETS: dict[str, Scenario] = {
    "baseline": Scenario(
        name="baseline",
        sensors=5,
        rate=1.0,
        batch_size=100,
        duration_s=60.0,
        sla=_STEADY_SLA,
    ),
    "burst": Scenario(
        name="burst",
        sensors=5,
        batch_size=500,
        duration_s=60.0,
        burst_size=150,
        burst_every_s=20.0,
        # Each burst overflows the bucket, so a run without 429s means the
        # limiter is off or misconfigured.
        sla=SlaThresholds(max_p99_ms=1000.0, max_error_rate=0.001, min_rate_limited_rate=0.05),
    ),
    "sustained": Scenario(
        name="sustained",
        transport="ws",
        sensors=20,
        rate=20.0,
        batch_size=100,
        duration_s=300.0,
        sla=_STEADY_SLA,
    ),
    "ceiling": Scenario(
        name="ceiling",
        sensors=50,
        rate=0.0,
        batch_size=500,
        duration_s=60.0,
        # Any 429 means the limiter, not the service, set the ceiling: lift
        # the limits first (loadtest/README.md).
        sla=SlaThresholds(max_error_rate=0.01, max_rate_limited_rate=0.0),
    ),
}
