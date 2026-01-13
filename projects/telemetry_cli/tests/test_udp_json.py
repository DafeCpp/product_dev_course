from __future__ import annotations

import unittest
from datetime import datetime, timezone

from telemetry_cli.sources.udp_json import _parse_ts


class TestUdpJson(unittest.TestCase):
    def test_parse_ts_ms(self) -> None:
        ts = _parse_ts({"ts_ms": 1000})
        self.assertEqual(ts, datetime(1970, 1, 1, 0, 0, 1, tzinfo=timezone.utc))

    def test_parse_timestamp_rfc3339_z(self) -> None:
        ts = _parse_ts({"timestamp": "2026-01-01T00:00:00Z"})
        self.assertEqual(ts, datetime(2026, 1, 1, 0, 0, 0, tzinfo=timezone.utc))

