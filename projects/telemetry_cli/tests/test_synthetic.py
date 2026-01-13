from __future__ import annotations

import asyncio
import unittest

from telemetry_cli.config import SourceSyntheticConfig, SyntheticSignalConfig
from telemetry_cli.sources.synthetic import _signal_value, synthetic_source


class TestSynthetic(unittest.IsolatedAsyncioTestCase):
    def test_signal_value_constant(self) -> None:
        sig = SyntheticSignalConfig(signal="x", kind="constant", offset=3.5)
        self.assertEqual(_signal_value(sig, t_s=123.0), 3.5)

    async def test_synthetic_source_emits_readings(self) -> None:
        cfg = SourceSyntheticConfig(
            sample_hz=1000.0,
            signals=[
                SyntheticSignalConfig(signal="imu.ax", kind="constant", offset=1.0),
                SyntheticSignalConfig(signal="imu.ay", kind="constant", offset=2.0),
            ],
        )

        gen = synthetic_source(cfg)
        r1 = await asyncio.wait_for(anext(gen), timeout=0.5)
        r2 = await asyncio.wait_for(anext(gen), timeout=0.5)

        self.assertEqual(r1.signal, "imu.ax")
        self.assertEqual(r2.signal, "imu.ay")
        self.assertEqual(r1.raw_value, 1.0)
        self.assertEqual(r2.raw_value, 2.0)

