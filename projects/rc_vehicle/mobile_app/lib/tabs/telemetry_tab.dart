import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:share_plus/share_plus.dart' show Share, XFile;
import '../models/telemetry_frame.dart';
import '../providers/connection_provider.dart';
import '../screens/settings_screen.dart';
import '../services/csv_writer.dart';
import '../services/udp_receiver.dart';
import '../widgets/telemetry_chart.dart';

class TelemetryTab extends ConsumerStatefulWidget {
  const TelemetryTab({super.key});

  @override
  ConsumerState<TelemetryTab> createState() => _TelemetryTabState();
}

class _TelemetryTabState extends ConsumerState<TelemetryTab>
    with WidgetsBindingObserver {
  final UdpReceiver _udp = UdpReceiver();
  final CsvWriter _csv = CsvWriter();
  StreamSubscription? _udpSub;
  Timer? _refreshTimer;
  bool _streaming = false;

  // Chart series — 1000 points = 10s at 100 Hz.
  // Downsampled to every 3rd frame for display (~33 Hz visual).
  int _downsampleCounter = 0;

  // Orientation
  final _pitch = ChartSeries(label: 'Pitch', color: Colors.red);
  final _roll = ChartSeries(label: 'Roll', color: Colors.green);
  final _yaw = ChartSeries(label: 'Yaw', color: Colors.blue);

  // Angular velocity
  final _gx = ChartSeries(label: 'Gx', color: Colors.red);
  final _gy = ChartSeries(label: 'Gy', color: Colors.green);
  final _gz = ChartSeries(label: 'Gz', color: Colors.blue);

  // Acceleration
  final _ax = ChartSeries(label: 'Ax', color: Colors.red);
  final _ay = ChartSeries(label: 'Ay', color: Colors.green);
  final _az = ChartSeries(label: 'Az', color: Colors.blue);

  // Speed & slip
  final _speed = ChartSeries(label: 'Speed', color: Colors.blue);
  final _slip = ChartSeries(label: 'Slip', color: Colors.red);

  // Control
  final _throttle = ChartSeries(label: 'Throttle', color: Colors.green);
  final _steering = ChartSeries(label: 'Steering', color: Colors.orange);

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _startStream();
    // Refresh UI at 30 FPS.
    _refreshTimer = Timer.periodic(
      const Duration(milliseconds: 33),
      (_) {
        if (mounted) setState(() {});
      },
    );
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _stopStream();
    _refreshTimer?.cancel();
    _csv.dispose();
    _udp.dispose();
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.paused) {
      _stopStream();
    } else if (state == AppLifecycleState.resumed) {
      _startStream();
    }
  }

  void _startStream() {
    if (_streaming) return;
    _streaming = true;

    final port = ref.read(appSettingsProvider).udpPort;
    _udp.start(port).then((_) {
      _udpSub = _udp.stream.listen(_onFrame);

      // Tell ESP32 to start UDP stream via WebSocket.
      ref.read(connectionProvider.notifier).sendCommand({
        'type': 'udp_stream_start',
        'ip': '', // ESP32 will use sender IP from the WebSocket connection
        'port': port,
        'hz': 100,
      });
    });
  }

  void _stopStream() {
    if (!_streaming) return;

    ref.read(connectionProvider.notifier).sendCommand({
      'type': 'udp_stream_stop',
    });
    _udpSub?.cancel();
    _udpSub = null;
    _udp.stop();
    _streaming = false;
  }

  void _onFrame(TelemetryFrame f) {
    // Write all frames to CSV if recording.
    if (_csv.isRecording) {
      _csv.writeFrame(f);
    }

    // Downsample for chart display: every 3rd frame.
    _downsampleCounter++;
    if (_downsampleCounter % 3 != 0) return;

    _pitch.add(f.pitchDeg);
    _roll.add(f.rollDeg);
    _yaw.add(f.yawDeg);

    _gx.add(f.gx);
    _gy.add(f.gy);
    _gz.add(f.gz);

    _ax.add(f.ax);
    _ay.add(f.ay);
    _az.add(f.az);

    _speed.add(f.speedMs);
    _slip.add(f.slipDeg);

    _throttle.add(f.throttle);
    _steering.add(f.steering);
  }

  Future<void> _toggleRecording() async {
    if (_csv.isRecording) {
      final path = await _csv.stopRecording();
      if (mounted && path != null) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Saved: ${path.split('/').last}'),
            action: SnackBarAction(
              label: 'Share',
              onPressed: () => _shareFile(path),
            ),
          ),
        );
      }
    } else {
      await _csv.startRecording();
    }
    setState(() {});
  }

  Future<void> _shareFile(String path) async {
    await Share.shareXFiles([XFile(path)]);
  }

  void _clearCharts() {
    for (final s in [
      _pitch, _roll, _yaw,
      _gx, _gy, _gz,
      _ax, _ay, _az,
      _speed, _slip,
      _throttle, _steering,
    ]) {
      s.clear();
    }
    _udp.stats.reset();
    _downsampleCounter = 0;
  }

  @override
  Widget build(BuildContext context) {
    final stats = _udp.stats;

    return Column(
      children: [
        // Charts (scrollable).
        Expanded(
          child: ListView(
            padding: const EdgeInsets.all(12),
            children: [
              TelemetryChart(
                title: 'Orientation (deg)',
                series: [_pitch, _roll, _yaw],
                minY: -180,
                maxY: 180,
              ),
              const SizedBox(height: 12),
              TelemetryChart(
                title: 'Angular velocity (dps)',
                series: [_gx, _gy, _gz],
              ),
              const SizedBox(height: 12),
              TelemetryChart(
                title: 'Acceleration (g)',
                series: [_ax, _ay, _az],
                minY: -4,
                maxY: 4,
              ),
              const SizedBox(height: 12),
              TelemetryChart(
                title: 'Speed & Slip',
                series: [_speed, _slip],
              ),
              const SizedBox(height: 12),
              TelemetryChart(
                title: 'Control',
                series: [_throttle, _steering],
                minY: -1,
                maxY: 1,
              ),
            ],
          ),
        ),

        // Bottom bar: stats + recording controls.
        Container(
          color: Theme.of(context).colorScheme.surfaceContainerHighest,
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          child: Row(
            children: [
              // Stats.
              _StatChip('${stats.hz.toStringAsFixed(0)} Hz'),
              const SizedBox(width: 8),
              _StatChip('Lost: ${stats.lost}'),
              const SizedBox(width: 8),
              _StatChip('${stats.lossPercent.toStringAsFixed(1)}%'),
              const Spacer(),

              // Recording.
              if (_csv.isRecording) ...[
                Icon(Icons.fiber_manual_record, color: Colors.red, size: 14),
                const SizedBox(width: 4),
                Text(
                  '${_csv.frameCount} frames  '
                  '${_csv.duration.inSeconds}s  '
                  '${(_csv.fileSizeEstimate / 1024).toStringAsFixed(0)} KB',
                  style: const TextStyle(fontSize: 12, color: Colors.red),
                ),
                const SizedBox(width: 8),
              ],

              // Buttons.
              IconButton(
                icon: const Icon(Icons.delete_outline, size: 20),
                tooltip: 'Clear charts',
                onPressed: _clearCharts,
              ),
              FilledButton.icon(
                icon: Icon(
                  _csv.isRecording ? Icons.stop : Icons.fiber_manual_record,
                  size: 16,
                ),
                label: Text(_csv.isRecording ? 'Stop' : 'Rec'),
                style: FilledButton.styleFrom(
                  backgroundColor:
                      _csv.isRecording ? Colors.red : null,
                  padding:
                      const EdgeInsets.symmetric(horizontal: 12),
                  minimumSize: const Size(0, 36),
                ),
                onPressed: _toggleRecording,
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _StatChip extends StatelessWidget {
  final String text;
  const _StatChip(this.text);

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerHigh,
        borderRadius: BorderRadius.circular(4),
      ),
      child: Text(
        text,
        style: Theme.of(context).textTheme.labelSmall,
      ),
    );
  }
}
