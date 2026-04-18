from __future__ import annotations

import unittest
from datetime import datetime, timezone
from typing import Any

from telemetry_cli.sources.esp32_ws import _flatten_esp32_telem


def _make_msg(**overrides: Any) -> dict[str, Any]:
    """Минимальное корректное telem-сообщение прошивки."""
    msg: dict[str, Any] = {
        "type": "telem",
        "ts_ms": 1_700_000_000_000,
        "link": {"rc_ok": True, "wifi_ok": False, "failsafe": False},
        "imu": {
            "ax": 0.1,
            "ay": -0.2,
            "az": 9.8,
            "gx": 1.0,
            "gy": -1.0,
            "gz": 0.5,
            "gyro_z_filtered": 0.3,
            "forward_accel": 0.05,
            "orientation": {"pitch": 2.0, "roll": -1.5, "yaw": 45.0},
        },
        "ekf": {"vx": 1.2, "vy": 0.1, "yaw_rate": 10.0, "slip_deg": 4.7, "speed_ms": 1.21},
        "act": {"throttle": 0.6, "steering": -0.1},
    }
    msg.update(overrides)
    return msg


def _signals(readings: list) -> dict[str, float]:
    return {r.signal: r.raw_value for r in readings}


class TestFlattenEsp32Telem(unittest.TestCase):
    # ------------------------------------------------------------------
    # ts_ms → timestamp
    # ------------------------------------------------------------------
    def test_timestamp_from_ts_ms(self) -> None:
        msg = _make_msg()
        readings = _flatten_esp32_telem(msg)
        expected_ts = datetime.fromtimestamp(1_700_000_000_000 / 1000.0, tz=timezone.utc)
        for r in readings:
            self.assertEqual(r.timestamp, expected_ts)

    def test_timestamp_fallback_to_utc_now_when_ts_ms_absent(self) -> None:
        msg = _make_msg()
        del msg["ts_ms"]
        readings = _flatten_esp32_telem(msg)
        self.assertGreater(len(readings), 0)
        for r in readings:
            self.assertIsNotNone(r.timestamp)

    # ------------------------------------------------------------------
    # IMU базовые поля
    # ------------------------------------------------------------------
    def test_imu_basic_fields_present(self) -> None:
        sigs = _signals(_flatten_esp32_telem(_make_msg()))
        for k in ("ax", "ay", "az", "gx", "gy", "gz"):
            self.assertIn(f"imu.{k}", sigs, msg=f"missing imu.{k}")

    def test_imu_basic_values(self) -> None:
        sigs = _signals(_flatten_esp32_telem(_make_msg()))
        self.assertAlmostEqual(sigs["imu.ax"], 0.1)
        self.assertAlmostEqual(sigs["imu.az"], 9.8)
        self.assertAlmostEqual(sigs["imu.gz"], 0.5)

    # ------------------------------------------------------------------
    # IMU расширенные поля (Фаза 3.7)
    # ------------------------------------------------------------------
    def test_imu_gyro_z_filtered(self) -> None:
        sigs = _signals(_flatten_esp32_telem(_make_msg()))
        self.assertIn("imu.gyro_z_filtered", sigs)
        self.assertAlmostEqual(sigs["imu.gyro_z_filtered"], 0.3)

    def test_imu_forward_accel(self) -> None:
        sigs = _signals(_flatten_esp32_telem(_make_msg()))
        self.assertIn("imu.forward_accel", sigs)
        self.assertAlmostEqual(sigs["imu.forward_accel"], 0.05)

    def test_imu_orientation_fields(self) -> None:
        sigs = _signals(_flatten_esp32_telem(_make_msg()))
        self.assertAlmostEqual(sigs["imu.orientation.pitch"], 2.0)
        self.assertAlmostEqual(sigs["imu.orientation.roll"], -1.5)
        self.assertAlmostEqual(sigs["imu.orientation.yaw"], 45.0)

    def test_imu_absent_skips_gracefully(self) -> None:
        msg = _make_msg()
        del msg["imu"]
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertFalse(any(s.startswith("imu.") for s in sigs))

    def test_imu_orientation_absent_skips_gracefully(self) -> None:
        msg = _make_msg()
        del msg["imu"]["orientation"]
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertFalse(any("orientation" in s for s in sigs))

    def test_imu_extended_fields_absent_skips_gracefully(self) -> None:
        msg = _make_msg()
        del msg["imu"]["gyro_z_filtered"]
        del msg["imu"]["forward_accel"]
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertNotIn("imu.gyro_z_filtered", sigs)
        self.assertNotIn("imu.forward_accel", sigs)
        # базовые поля всё ещё есть
        self.assertIn("imu.ax", sigs)

    # ------------------------------------------------------------------
    # EKF поля (Фаза 3.7)
    # ------------------------------------------------------------------
    def test_ekf_fields_present(self) -> None:
        sigs = _signals(_flatten_esp32_telem(_make_msg()))
        for k in ("vx", "vy", "yaw_rate", "slip_deg", "speed_ms"):
            self.assertIn(f"ekf.{k}", sigs, msg=f"missing ekf.{k}")

    def test_ekf_values(self) -> None:
        sigs = _signals(_flatten_esp32_telem(_make_msg()))
        self.assertAlmostEqual(sigs["ekf.vx"], 1.2)
        self.assertAlmostEqual(sigs["ekf.vy"], 0.1)
        self.assertAlmostEqual(sigs["ekf.yaw_rate"], 10.0)
        self.assertAlmostEqual(sigs["ekf.slip_deg"], 4.7)
        self.assertAlmostEqual(sigs["ekf.speed_ms"], 1.21)

    def test_ekf_absent_skips_gracefully(self) -> None:
        msg = _make_msg()
        del msg["ekf"]
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertFalse(any(s.startswith("ekf.") for s in sigs))

    def test_ekf_partial_fields(self) -> None:
        msg = _make_msg()
        msg["ekf"] = {"vx": 2.5, "slip_deg": -3.0}
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertIn("ekf.vx", sigs)
        self.assertIn("ekf.slip_deg", sigs)
        self.assertNotIn("ekf.vy", sigs)

    def test_ekf_negative_slip(self) -> None:
        msg = _make_msg()
        msg["ekf"]["slip_deg"] = -12.3
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertAlmostEqual(sigs["ekf.slip_deg"], -12.3)

    # ------------------------------------------------------------------
    # act поля — правильные ключи throttle/steering (Фаза 3.7: фикс)
    # ------------------------------------------------------------------
    def test_act_throttle_and_steering(self) -> None:
        sigs = _signals(_flatten_esp32_telem(_make_msg()))
        self.assertIn("act.throttle", sigs)
        self.assertIn("act.steering", sigs)
        self.assertAlmostEqual(sigs["act.throttle"], 0.6)
        self.assertAlmostEqual(sigs["act.steering"], -0.1)

    def test_act_old_keys_thr_steer_not_parsed(self) -> None:
        msg = _make_msg()
        msg["act"] = {"thr": 0.5, "steer": 0.2}
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertNotIn("act.thr", sigs)
        self.assertNotIn("act.steer", sigs)
        self.assertNotIn("act.throttle", sigs)
        self.assertNotIn("act.steering", sigs)

    def test_act_absent_skips_gracefully(self) -> None:
        msg = _make_msg()
        del msg["act"]
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertFalse(any(s.startswith("act.") for s in sigs))

    # ------------------------------------------------------------------
    # link поля
    # ------------------------------------------------------------------
    def test_link_rc_ok_true(self) -> None:
        sigs = _signals(_flatten_esp32_telem(_make_msg()))
        self.assertAlmostEqual(sigs["link.rc_ok"], 1.0)

    def test_link_wifi_ok_false(self) -> None:
        sigs = _signals(_flatten_esp32_telem(_make_msg()))
        self.assertAlmostEqual(sigs["link.wifi_ok"], 0.0)

    def test_link_failsafe(self) -> None:
        msg = _make_msg()
        msg["link"]["failsafe"] = True
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertIn("link.failsafe", sigs)
        self.assertAlmostEqual(sigs["link.failsafe"], 1.0)

    def test_link_failsafe_false(self) -> None:
        sigs = _signals(_flatten_esp32_telem(_make_msg()))
        self.assertAlmostEqual(sigs["link.failsafe"], 0.0)

    def test_link_active_source_not_emitted(self) -> None:
        # Прошивка не шлёт active_source — парсер не должен его генерировать
        sigs = _signals(_flatten_esp32_telem(_make_msg()))
        self.assertNotIn("link.active_source", sigs)

    def test_link_absent_skips_gracefully(self) -> None:
        msg = _make_msg()
        del msg["link"]
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertFalse(any(s.startswith("link.") for s in sigs))

    # ------------------------------------------------------------------
    # Общие edge cases
    # ------------------------------------------------------------------
    def test_empty_message_returns_empty_list(self) -> None:
        readings = _flatten_esp32_telem({})
        self.assertEqual(readings, [])

    def test_non_numeric_imu_value_skipped(self) -> None:
        msg = _make_msg()
        msg["imu"]["ax"] = "bad"
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertNotIn("imu.ax", sigs)
        # остальные IMU-поля должны быть
        self.assertIn("imu.ay", sigs)

    def test_non_numeric_ekf_value_skipped(self) -> None:
        msg = _make_msg()
        msg["ekf"]["vx"] = None
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertNotIn("ekf.vx", sigs)
        self.assertIn("ekf.vy", sigs)

    def test_signal_names_are_strings(self) -> None:
        readings = _flatten_esp32_telem(_make_msg())
        for r in readings:
            self.assertIsInstance(r.signal, str)

    def test_all_raw_values_are_float(self) -> None:
        readings = _flatten_esp32_telem(_make_msg())
        for r in readings:
            self.assertIsInstance(r.raw_value, float)

    # ------------------------------------------------------------------
    # warn поля (Фаза 4.2: oversteer prediction)
    # ------------------------------------------------------------------
    def test_warn_oversteer_true(self) -> None:
        msg = _make_msg()
        msg["warn"] = {"oversteer": True}
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertIn("warn.oversteer", sigs)
        self.assertAlmostEqual(sigs["warn.oversteer"], 1.0)

    def test_warn_oversteer_false(self) -> None:
        msg = _make_msg()
        msg["warn"] = {"oversteer": False}
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertIn("warn.oversteer", sigs)
        self.assertAlmostEqual(sigs["warn.oversteer"], 0.0)

    def test_warn_absent_skips_gracefully(self) -> None:
        msg = _make_msg()
        # warn отсутствует — нет сигналов warn.*
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertFalse(any(s.startswith("warn.") for s in sigs))

    def test_warn_unknown_fields_not_emitted(self) -> None:
        msg = _make_msg()
        msg["warn"] = {"oversteer": False, "unknown_flag": True}
        sigs = _signals(_flatten_esp32_telem(msg))
        self.assertNotIn("warn.unknown_flag", sigs)
        self.assertIn("warn.oversteer", sigs)

    def test_warn_oversteer_raw_value_is_float(self) -> None:
        msg = _make_msg()
        msg["warn"] = {"oversteer": True}
        readings = _flatten_esp32_telem(msg)
        warn_reading = next(r for r in readings if r.signal == "warn.oversteer")
        self.assertIsInstance(warn_reading.raw_value, float)

    def test_warn_timestamp_matches_ts_ms(self) -> None:
        msg = _make_msg()
        msg["warn"] = {"oversteer": True}
        expected_ts = datetime.fromtimestamp(1_700_000_000_000 / 1000.0, tz=timezone.utc)
        readings = _flatten_esp32_telem(msg)
        warn_reading = next(r for r in readings if r.signal == "warn.oversteer")
        self.assertEqual(warn_reading.timestamp, expected_ts)


if __name__ == "__main__":
    unittest.main()
