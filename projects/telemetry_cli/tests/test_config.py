from __future__ import annotations

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from telemetry_cli.config import load_config


class TestConfig(unittest.TestCase):
    def test_load_config_valid_yaml(self) -> None:
        with TemporaryDirectory() as td:
            cfg_path = Path(td) / "cfg.yaml"
            cfg_path.write_text(
                """
experiment_service:
  base_url: "http://localhost:8003"
  sensor_token: "secret"
target:
  sensor_id: "00000000-0000-0000-0000-000000000001"
batch:
  max_readings: 3
  flush_interval_ms: 50
source:
  type: "synthetic"
  sample_hz: 1000
  signals:
    - signal: "imu.ax"
      kind: "constant"
      offset: 1.0
""".lstrip(),
                encoding="utf-8",
            )

            cfg = load_config(cfg_path)
            self.assertEqual(cfg.target.sensor_id.int, 1)
            self.assertEqual(cfg.source.type, "synthetic")

