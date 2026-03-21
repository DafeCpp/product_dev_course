import 'dart:typed_data';

/// Parsed UDP telemetry frame from ESP32.
/// Packet layout (79 bytes total):
///   [0..1]  magic 0x52, 0x54 ("RT")
///   [2]     version 0x01
///   [3..6]  seq (uint32 LE)
///   [7..78] TelemetryLogFrame (72 bytes, all LE)
class TelemetryFrame {
  final int seqNum;
  final int tsMs;
  final double ax, ay, az;
  final double gx, gy, gz;
  final double vx, vy;
  final double slipDeg;
  final double speedMs;
  final double throttle;
  final double steering;
  final double pitchDeg, rollDeg, yawDeg;
  final double yawRateDps;
  final bool oversteerActive;

  const TelemetryFrame({
    required this.seqNum,
    required this.tsMs,
    required this.ax,
    required this.ay,
    required this.az,
    required this.gx,
    required this.gy,
    required this.gz,
    required this.vx,
    required this.vy,
    required this.slipDeg,
    required this.speedMs,
    required this.throttle,
    required this.steering,
    required this.pitchDeg,
    required this.rollDeg,
    required this.yawDeg,
    required this.yawRateDps,
    required this.oversteerActive,
  });

  static const int packetSize = 79;
  static const int magicByte0 = 0x52; // 'R'
  static const int magicByte1 = 0x54; // 'T'
  static const int currentVersion = 0x01;

  /// Parse a 79-byte UDP packet into a TelemetryFrame.
  /// Returns null if magic/version mismatch or wrong size.
  static TelemetryFrame? fromBytes(Uint8List data) {
    if (data.length < packetSize) return null;
    if (data[0] != magicByte0 || data[1] != magicByte1) return null;
    if (data[2] != currentVersion) return null;

    final bd = ByteData.sublistView(data);
    int o = 7; // offset into TelemetryLogFrame

    return TelemetryFrame(
      seqNum: bd.getUint32(3, Endian.little),
      tsMs: bd.getUint32(o, Endian.little),
      ax: bd.getFloat32(o + 4, Endian.little),
      ay: bd.getFloat32(o + 8, Endian.little),
      az: bd.getFloat32(o + 12, Endian.little),
      gx: bd.getFloat32(o + 16, Endian.little),
      gy: bd.getFloat32(o + 20, Endian.little),
      gz: bd.getFloat32(o + 24, Endian.little),
      vx: bd.getFloat32(o + 28, Endian.little),
      vy: bd.getFloat32(o + 32, Endian.little),
      slipDeg: bd.getFloat32(o + 36, Endian.little),
      speedMs: bd.getFloat32(o + 40, Endian.little),
      throttle: bd.getFloat32(o + 44, Endian.little),
      steering: bd.getFloat32(o + 48, Endian.little),
      pitchDeg: bd.getFloat32(o + 52, Endian.little),
      rollDeg: bd.getFloat32(o + 56, Endian.little),
      yawDeg: bd.getFloat32(o + 60, Endian.little),
      yawRateDps: bd.getFloat32(o + 64, Endian.little),
      oversteerActive: bd.getFloat32(o + 68, Endian.little) >= 0.5,
    );
  }

  /// Parse JSON telemetry (from WebSocket 20 Hz stream).
  factory TelemetryFrame.fromJson(Map<String, dynamic> j) {
    return TelemetryFrame(
      seqNum: 0,
      tsMs: (j['ts_ms'] as num?)?.toInt() ?? 0,
      ax: (j['ax'] as num?)?.toDouble() ?? 0,
      ay: (j['ay'] as num?)?.toDouble() ?? 0,
      az: (j['az'] as num?)?.toDouble() ?? 0,
      gx: (j['gx'] as num?)?.toDouble() ?? 0,
      gy: (j['gy'] as num?)?.toDouble() ?? 0,
      gz: (j['gz'] as num?)?.toDouble() ?? 0,
      vx: (j['vx'] as num?)?.toDouble() ?? 0,
      vy: (j['vy'] as num?)?.toDouble() ?? 0,
      slipDeg: (j['slip_deg'] as num?)?.toDouble() ?? 0,
      speedMs: (j['speed_ms'] as num?)?.toDouble() ?? 0,
      throttle: (j['throttle'] as num?)?.toDouble() ?? 0,
      steering: (j['steering'] as num?)?.toDouble() ?? 0,
      pitchDeg: (j['pitch_deg'] as num?)?.toDouble() ?? 0,
      rollDeg: (j['roll_deg'] as num?)?.toDouble() ?? 0,
      yawDeg: (j['yaw_deg'] as num?)?.toDouble() ?? 0,
      yawRateDps: (j['yaw_rate_dps'] as num?)?.toDouble() ?? 0,
      oversteerActive: j['oversteer_active'] == true ||
          j['oversteer_active'] == 1 ||
          (j['oversteer_active'] is num &&
              (j['oversteer_active'] as num) >= 0.5),
    );
  }
}
