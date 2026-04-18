import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:wakelock_plus/wakelock_plus.dart';
import '../models/telemetry_frame.dart';
import '../providers/connection_provider.dart';
import '../providers/drive_input_provider.dart';
import '../widgets/connection_indicator.dart';
import '../widgets/joystick.dart';
import '../widgets/mini_chart.dart';
import '../widgets/throttle_zone.dart';

class DriveTab extends ConsumerStatefulWidget {
  const DriveTab({super.key});

  @override
  ConsumerState<DriveTab> createState() => _DriveTabState();
}

class _DriveTabState extends ConsumerState<DriveTab> {
  bool _showOverlay = false;

  // Mini-chart ring buffers (200 points ~ 10s at 20Hz).
  final _speedData = MiniChartData(color: Colors.cyanAccent);
  final _slipData = MiniChartData(color: Colors.orangeAccent);
  final _pitchData = MiniChartData(color: Colors.redAccent);
  final _rollData = MiniChartData(color: Colors.greenAccent);
  final _thrData = MiniChartData(color: Colors.lightGreenAccent);
  final _strData = MiniChartData(color: Colors.amberAccent);

  Timer? _refreshTimer;
  TelemetryFrame? _lastFrame;

  @override
  void initState() {
    super.initState();
    _enterDriveMode();
    // Cap overlay refresh at 15 fps to save battery.
    _refreshTimer = Timer.periodic(const Duration(milliseconds: 66), (_) {
      if (_showOverlay && mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _refreshTimer?.cancel();
    _exitDriveMode();
    super.dispose();
  }

  void _enterDriveMode() {
    SystemChrome.setPreferredOrientations([
      DeviceOrientation.landscapeLeft,
      DeviceOrientation.landscapeRight,
    ]);
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
    WakelockPlus.enable();
  }

  void _exitDriveMode() {
    SystemChrome.setPreferredOrientations(DeviceOrientation.values);
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
    WakelockPlus.disable();
    ref.read(driveInputProvider.notifier).reset();
    ref.read(connectionProvider.notifier).setDriveInput(0, 0);
  }

  void _onThrottleChanged(double value) {
    ref.read(driveInputProvider.notifier).setThrottle(value);
    final input = ref.read(driveInputProvider);
    ref.read(connectionProvider.notifier).setDriveInput(
      input.throttle,
      input.steering,
    );
  }

  void _onSteeringChanged(double value) {
    ref.read(driveInputProvider.notifier).setSteering(value);
    final input = ref.read(driveInputProvider);
    ref.read(connectionProvider.notifier).setDriveInput(
      input.throttle,
      input.steering,
    );
  }

  void _feedCharts(TelemetryFrame? frame) {
    if (frame == null || frame == _lastFrame) return;
    _lastFrame = frame;
    _speedData.add(frame.speedMs);
    _slipData.add(frame.slipDeg);
    _pitchData.add(frame.pitchDeg);
    _rollData.add(frame.rollDeg);
    _thrData.add(frame.throttle);
    _strData.add(frame.steering);
  }

  @override
  Widget build(BuildContext context) {
    final telem = ref.watch(telemetryProvider);
    final input = ref.watch(driveInputProvider);
    final conn = ref.watch(connectionProvider);

    // Feed chart data on every telemetry update.
    _feedCharts(telem);

    return Scaffold(
      backgroundColor: Colors.black,
      body: Column(
        children: [
          _StatusBar(telemetry: telem, connected: conn.isConnected),
          Expanded(
            child: Row(
              children: [
                // Left: throttle/brake zone.
                SizedBox(
                  width: 120,
                  child: Padding(
                    padding: const EdgeInsets.all(8),
                    child: ThrottleZone(onChanged: _onThrottleChanged),
                  ),
                ),

                // Center: info + overlay charts.
                Expanded(
                  child: Stack(
                    children: [
                      _CenterInfo(
                        telemetry: telem,
                        throttle: input.throttle,
                        steering: input.steering,
                      ),
                      if (_showOverlay)
                        Positioned.fill(
                          child: _TelemetryOverlay(
                            speedData: _speedData,
                            slipData: _slipData,
                            pitchData: _pitchData,
                            rollData: _rollData,
                            thrData: _thrData,
                            strData: _strData,
                          ),
                        ),
                    ],
                  ),
                ),

                // Right: steering joystick.
                SizedBox(
                  width: 200,
                  child: Padding(
                    padding: const EdgeInsets.all(8),
                    child: SteeringJoystick(onChanged: _onSteeringChanged),
                  ),
                ),
              ],
            ),
          ),
          _BottomBar(
            connected: conn.isConnected,
            overlayActive: _showOverlay,
            onToggleOverlay: () => setState(() => _showOverlay = !_showOverlay),
          ),
        ],
      ),
    );
  }
}

// --- Telemetry overlay ---

class _TelemetryOverlay extends StatelessWidget {
  final MiniChartData speedData;
  final MiniChartData slipData;
  final MiniChartData pitchData;
  final MiniChartData rollData;
  final MiniChartData thrData;
  final MiniChartData strData;

  const _TelemetryOverlay({
    required this.speedData,
    required this.slipData,
    required this.pitchData,
    required this.rollData,
    required this.thrData,
    required this.strData,
  });

  @override
  Widget build(BuildContext context) {
    return IgnorePointer(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.spaceEvenly,
          children: [
            _OverlayRow(
              label: 'SPD / SLIP',
              chart: MiniChart(
                series: [speedData, slipData],
                minY: -5,
                maxY: 5,
                height: 36,
              ),
            ),
            _OverlayRow(
              label: 'PITCH / ROLL',
              chart: MiniChart(
                series: [pitchData, rollData],
                minY: -30,
                maxY: 30,
                height: 36,
              ),
            ),
            _OverlayRow(
              label: 'THR / STR',
              chart: MiniChart(
                series: [thrData, strData],
                minY: -1,
                maxY: 1,
                height: 36,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _OverlayRow extends StatelessWidget {
  final String label;
  final Widget chart;

  const _OverlayRow({required this.label, required this.chart});

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        SizedBox(
          width: 64,
          child: Text(
            label,
            style: TextStyle(
              color: Colors.white.withValues(alpha: 0.4),
              fontSize: 8,
              fontWeight: FontWeight.w600,
            ),
          ),
        ),
        Expanded(child: chart),
      ],
    );
  }
}

// --- Status bar ---

class _StatusBar extends StatelessWidget {
  final TelemetryFrame? telemetry;
  final bool connected;

  const _StatusBar({required this.telemetry, required this.connected});

  @override
  Widget build(BuildContext context) {
    final speed = telemetry?.speedMs ?? 0;
    final slip = telemetry?.slipDeg ?? 0;

    return Container(
      height: 28,
      color: Colors.black87,
      padding: const EdgeInsets.symmetric(horizontal: 12),
      child: Row(
        children: [
          const ConnectionIndicator(),
          const Spacer(),
          Text(
            '${speed.toStringAsFixed(1)} m/s',
            style: const TextStyle(color: Colors.white70, fontSize: 12),
          ),
          const SizedBox(width: 16),
          Text(
            'Slip: ${slip.toStringAsFixed(0)}°',
            style: TextStyle(
              color: slip.abs() > 10 ? Colors.orange : Colors.white70,
              fontSize: 12,
            ),
          ),
          const SizedBox(width: 16),
          if (telemetry?.oversteerActive == true)
            const Text(
              'OVERSTEER',
              style: TextStyle(
                color: Colors.red,
                fontSize: 12,
                fontWeight: FontWeight.bold,
              ),
            ),
        ],
      ),
    );
  }
}

// --- Center info ---

class _CenterInfo extends StatelessWidget {
  final TelemetryFrame? telemetry;
  final double throttle;
  final double steering;

  const _CenterInfo({
    required this.telemetry,
    required this.throttle,
    required this.steering,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        if (telemetry != null) ...[
          Text(
            '${telemetry!.speedMs.toStringAsFixed(1)} m/s',
            style: const TextStyle(
              color: Colors.white,
              fontSize: 48,
              fontWeight: FontWeight.w200,
            ),
          ),
          const SizedBox(height: 8),
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              _InfoChip('Pitch', '${telemetry!.pitchDeg.toStringAsFixed(1)}°'),
              const SizedBox(width: 12),
              _InfoChip('Roll', '${telemetry!.rollDeg.toStringAsFixed(1)}°'),
              const SizedBox(width: 12),
              _InfoChip('Yaw', '${telemetry!.yawDeg.toStringAsFixed(0)}°'),
            ],
          ),
        ] else
          const Text(
            'No telemetry',
            style: TextStyle(color: Colors.white38, fontSize: 16),
          ),
        const SizedBox(height: 16),
        Text(
          'THR: ${(throttle * 100).round()}%  STR: ${(steering * 100).round()}%',
          style: const TextStyle(color: Colors.white54, fontSize: 13),
        ),
      ],
    );
  }
}

class _InfoChip extends StatelessWidget {
  final String label;
  final String value;
  const _InfoChip(this.label, this.value);

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Text(label, style: const TextStyle(color: Colors.white38, fontSize: 10)),
        Text(value, style: const TextStyle(color: Colors.white70, fontSize: 14)),
      ],
    );
  }
}

// --- E-Stop ---

class _EStopButton extends ConsumerWidget {
  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return TextButton(
      onPressed: () {
        ref.read(driveInputProvider.notifier).reset();
        ref.read(connectionProvider.notifier).setDriveInput(0, 0);
        ref.read(connectionProvider.notifier).sendCommand({
          'type': 'cmd',
          'throttle': 0.0,
          'steering': 0.0,
        });
      },
      style: TextButton.styleFrom(
        foregroundColor: Colors.red,
        padding: const EdgeInsets.symmetric(horizontal: 8),
        minimumSize: Size.zero,
      ),
      child: const Text('STOP', style: TextStyle(fontWeight: FontWeight.bold)),
    );
  }
}

// --- Bottom bar ---

class _BottomBar extends StatelessWidget {
  final bool connected;
  final bool overlayActive;
  final VoidCallback onToggleOverlay;

  const _BottomBar({
    required this.connected,
    required this.overlayActive,
    required this.onToggleOverlay,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 36,
      color: Colors.black87,
      padding: const EdgeInsets.symmetric(horizontal: 12),
      child: Row(
        children: [
          _EStopButton(),
          const SizedBox(width: 8),
          // Overlay toggle.
          GestureDetector(
            onTap: onToggleOverlay,
            child: Icon(
              Icons.show_chart,
              size: 20,
              color: overlayActive
                  ? Colors.cyanAccent
                  : Colors.white30,
            ),
          ),
          const Spacer(),
          TextButton(
            onPressed: () => Navigator.of(context).maybePop(),
            style: TextButton.styleFrom(
              foregroundColor: Colors.white54,
              padding: const EdgeInsets.symmetric(horizontal: 8),
              minimumSize: Size.zero,
            ),
            child: const Text('EXIT'),
          ),
        ],
      ),
    );
  }
}
