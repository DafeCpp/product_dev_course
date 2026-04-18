import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:rc_vehicle_app/models/telemetry_frame.dart';

void main() {
  group('TelemetryFrame.fromBytes', () {
    Uint8List buildPacket({
      int seq = 42,
      int tsMs = 1000,
      double throttle = 0.5,
      double steering = -0.3,
    }) {
      final data = Uint8List(87);
      final bd = ByteData.sublistView(data);
      data[0] = 0x52; // R
      data[1] = 0x54; // T
      data[2] = 0x01; // version
      bd.setUint32(3, seq, Endian.little);
      // TelemetryLogFrame starts at offset 7.
      int o = 7;
      bd.setUint32(o, tsMs, Endian.little); // ts_ms
      // ax, ay, az
      bd.setFloat32(o + 4, 0.1, Endian.little);
      bd.setFloat32(o + 8, -0.2, Endian.little);
      bd.setFloat32(o + 12, 1.0, Endian.little);
      // gx, gy, gz
      bd.setFloat32(o + 16, 10.0, Endian.little);
      bd.setFloat32(o + 20, -5.0, Endian.little);
      bd.setFloat32(o + 24, 0.0, Endian.little);
      // vx, vy
      bd.setFloat32(o + 28, 1.5, Endian.little);
      bd.setFloat32(o + 32, 0.1, Endian.little);
      // slip_deg, speed_ms
      bd.setFloat32(o + 36, 5.5, Endian.little);
      bd.setFloat32(o + 40, 2.0, Endian.little);
      // throttle, steering
      bd.setFloat32(o + 44, throttle, Endian.little);
      bd.setFloat32(o + 48, steering, Endian.little);
      // pitch, roll, yaw
      bd.setFloat32(o + 52, 3.0, Endian.little);
      bd.setFloat32(o + 56, -1.5, Endian.little);
      bd.setFloat32(o + 60, 90.0, Endian.little);
      // yaw_rate, oversteer_active
      bd.setFloat32(o + 64, 15.0, Endian.little);
      bd.setFloat32(o + 68, 0.0, Endian.little);
      // rc_throttle, rc_steering
      bd.setFloat32(o + 72, 0.6, Endian.little);
      bd.setFloat32(o + 76, -0.4, Endian.little);
      return data;
    }

    test('parses valid packet correctly', () {
      final packet = buildPacket(seq: 100, tsMs: 5000, throttle: 0.75);
      final frame = TelemetryFrame.fromBytes(packet);
      expect(frame, isNotNull);
      expect(frame!.seqNum, 100);
      expect(frame.tsMs, 5000);
      expect(frame.ax, closeTo(0.1, 0.001));
      expect(frame.ay, closeTo(-0.2, 0.001));
      expect(frame.az, closeTo(1.0, 0.001));
      expect(frame.throttle, closeTo(0.75, 0.001));
      expect(frame.steering, closeTo(-0.3, 0.001));
      expect(frame.speedMs, closeTo(2.0, 0.001));
      expect(frame.yawDeg, closeTo(90.0, 0.001));
      expect(frame.oversteerActive, false);
      expect(frame.rcThrottle, closeTo(0.6, 0.001));
      expect(frame.rcSteering, closeTo(-0.4, 0.001));
    });

    test('returns null on wrong magic bytes', () {
      final packet = buildPacket();
      packet[0] = 0x00;
      expect(TelemetryFrame.fromBytes(packet), isNull);
    });

    test('returns null on wrong version', () {
      final packet = buildPacket();
      packet[2] = 0x02;
      expect(TelemetryFrame.fromBytes(packet), isNull);
    });

    test('returns null on short packet', () {
      expect(TelemetryFrame.fromBytes(Uint8List(10)), isNull);
    });

    test('oversteer_active = true when float >= 0.5', () {
      final packet = buildPacket();
      final bd = ByteData.sublistView(packet);
      bd.setFloat32(7 + 68, 1.0, Endian.little);
      final frame = TelemetryFrame.fromBytes(packet);
      expect(frame!.oversteerActive, true);
    });
  });

  group('TelemetryFrame.fromJson', () {
    test('parses JSON telemetry', () {
      final json = {
        'ts_ms': 1234,
        'ax': 0.1,
        'ay': -0.2,
        'az': 1.0,
        'gx': 10.0,
        'gy': -5.0,
        'gz': 0.0,
        'vx': 1.5,
        'vy': 0.1,
        'slip_deg': 5.5,
        'speed_ms': 2.0,
        'throttle': 0.5,
        'steering': -0.3,
        'pitch_deg': 3.0,
        'roll_deg': -1.5,
        'yaw_deg': 90.0,
        'yaw_rate_dps': 15.0,
        'oversteer_active': false,
        'rc_throttle': 0.8,
        'rc_steering': -0.5,
      };
      final frame = TelemetryFrame.fromJson(json);
      expect(frame.tsMs, 1234);
      expect(frame.throttle, closeTo(0.5, 0.001));
      expect(frame.oversteerActive, false);
      expect(frame.rcThrottle, closeTo(0.8, 0.001));
      expect(frame.rcSteering, closeTo(-0.5, 0.001));
    });
  });
}
