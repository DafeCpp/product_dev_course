import 'dart:io';
import 'package:path_provider/path_provider.dart';
import '../models/telemetry_frame.dart';

const _csvHeader =
    'ts_ms,ax,ay,az,gx,gy,gz,vx,vy,slip_deg,speed_ms,'
    'throttle,steering,pitch_deg,roll_deg,yaw_deg,yaw_rate_dps,'
    'oversteer_active';

class CsvWriter {
  IOSink? _sink;
  File? _file;
  int _frameCount = 0;
  final List<String> _buffer = [];
  static const int _flushThreshold = 100;
  DateTime? _startTime;

  bool get isRecording => _sink != null;
  int get frameCount => _frameCount;
  String? get filePath => _file?.path;

  Duration get duration {
    if (_startTime == null) return Duration.zero;
    return DateTime.now().difference(_startTime!);
  }

  int get fileSizeEstimate => _frameCount * 120; // ~120 bytes per line

  Future<String> startRecording() async {
    final dir = await getApplicationDocumentsDirectory();
    final telemDir = Directory('${dir.path}/telemetry');
    if (!await telemDir.exists()) {
      await telemDir.create(recursive: true);
    }

    final now = DateTime.now();
    final name = '${now.year}-'
        '${now.month.toString().padLeft(2, '0')}-'
        '${now.day.toString().padLeft(2, '0')}_'
        '${now.hour.toString().padLeft(2, '0')}-'
        '${now.minute.toString().padLeft(2, '0')}-'
        '${now.second.toString().padLeft(2, '0')}.csv';

    _file = File('${telemDir.path}/$name');
    _sink = _file!.openWrite();
    _sink!.writeln(_csvHeader);
    _frameCount = 0;
    _buffer.clear();
    _startTime = now;

    return _file!.path;
  }

  void writeFrame(TelemetryFrame f) {
    if (_sink == null) return;

    _buffer.add(
      '${f.tsMs},'
      '${f.ax},${f.ay},${f.az},'
      '${f.gx},${f.gy},${f.gz},'
      '${f.vx},${f.vy},'
      '${f.slipDeg},${f.speedMs},'
      '${f.throttle},${f.steering},'
      '${f.pitchDeg},${f.rollDeg},${f.yawDeg},'
      '${f.yawRateDps},'
      '${f.oversteerActive ? 1 : 0}',
    );
    _frameCount++;

    if (_buffer.length >= _flushThreshold) {
      _flush();
    }
  }

  void _flush() {
    if (_sink == null || _buffer.isEmpty) return;
    _sink!.write('${_buffer.join('\n')}\n');
    _buffer.clear();
  }

  Future<String?> stopRecording() async {
    if (_sink == null) return null;
    _flush();
    await _sink!.flush();
    await _sink!.close();
    _sink = null;
    _startTime = null;
    return _file?.path;
  }

  Future<void> dispose() async {
    await stopRecording();
  }
}
