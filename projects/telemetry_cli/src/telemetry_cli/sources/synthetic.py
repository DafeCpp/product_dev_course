from __future__ import annotations

import asyncio
import math
import random
from collections.abc import AsyncIterator
from datetime import timedelta

from telemetry_cli.config import SourceSyntheticConfig, SyntheticSignalConfig
from telemetry_cli.models import TelemetryReading, utc_now


def _signal_value(sig: SyntheticSignalConfig, *, t_s: float) -> float:
    if sig.kind == "constant":
        return sig.offset

    if sig.kind == "noise":
        return sig.offset + random.gauss(0.0, sig.noise_std)

    w = 2.0 * math.pi * sig.freq_hz
    x = w * t_s + sig.phase_rad

    if sig.kind == "sine":
        return sig.offset + sig.amplitude * math.sin(x)

    if sig.kind == "square":
        # 0..1 phase
        frac = (t_s * sig.freq_hz + (sig.phase_rad / (2.0 * math.pi))) % 1.0
        return sig.offset + (sig.amplitude if frac < sig.duty else -sig.amplitude)

    if sig.kind == "saw":
        frac = (t_s * sig.freq_hz + (sig.phase_rad / (2.0 * math.pi))) % 1.0
        # -1..1 saw
        saw = 2.0 * frac - 1.0
        return sig.offset + sig.amplitude * saw

    raise ValueError(f"Unsupported synthetic kind: {sig.kind}")


async def synthetic_source(cfg: SourceSyntheticConfig) -> AsyncIterator[TelemetryReading]:
    """
    Emits synthetic multi-signal readings with one timestamp per sample tick.
    """
    if not cfg.signals:
        raise ValueError("synthetic.signals must not be empty")

    dt = 1.0 / cfg.sample_hz
    t0 = utc_now()
    n = 0
    while True:
        ts = t0 + timedelta(seconds=n * dt)
        t_s = n * dt
        for sig in cfg.signals:
            yield TelemetryReading(timestamp=ts, raw_value=_signal_value(sig, t_s=t_s), signal=sig.signal)
        n += 1
        await asyncio.sleep(dt)


