from __future__ import annotations

import unittest
from datetime import datetime, timezone

from telemetry_cli.models import TelemetryReading


class TestTelemetryReading(unittest.TestCase):
    def test_as_ingest_dict_includes_signal_and_rfc3339_z(self) -> None:
        r = TelemetryReading(
            timestamp=datetime(2026, 1, 1, 0, 0, 0, tzinfo=timezone.utc),
            raw_value=1.23,
            signal="imu.ax",
            meta={"seq": 123},
        )
        payload = r.as_ingest_dict()

        self.assertTrue(payload["timestamp"].endswith("Z"))
        self.assertEqual(payload["raw_value"], 1.23)
        self.assertEqual(payload["meta"]["signal"], "imu.ax")
        self.assertEqual(payload["meta"]["seq"], 123)

