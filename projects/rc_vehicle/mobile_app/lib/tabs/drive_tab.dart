import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:wakelock_plus/wakelock_plus.dart';
import '../models/telemetry_frame.dart';
import '../providers/connection_provider.dart';
import '../providers/drive_input_provider.dart';
import '../widgets/connection_indicator.dart';
import '../widgets/joystick.dart';
import '../widgets/throttle_zone.dart';

class DriveTab extends ConsumerStatefulWidget {
  const DriveTab({super.key});

  @override
  ConsumerState<DriveTab> createState() => _DriveTabState();
}

class _DriveTabState extends ConsumerState<DriveTab> {
  @override
  void initState() {
    super.initState();
    _enterDriveMode();
  }

  @override
  void dispose() {
    _exitDriveMode();
    super.dispose();
  }

  void _enterDriveMode() {
    // Landscape + immersive + wake lock.
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
    // Reset drive input on exit.
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

  @override
  Widget build(BuildContext context) {
    final telem = ref.watch(telemetryProvider);
    final input = ref.watch(driveInputProvider);
    final conn = ref.watch(connectionProvider);

    return Scaffold(
      backgroundColor: Colors.black,
      body: Column(
        children: [
          // Status bar (thin).
          _StatusBar(telemetry: telem, connected: conn.isConnected),

          // Main controls area.
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

                // Center: telemetry info.
                Expanded(
                  child: _CenterInfo(
                    telemetry: telem,
                    throttle: input.throttle,
                    steering: input.steering,
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

          // Bottom bar.
          _BottomBar(connected: conn.isConnected),
        ],
      ),
    );
  }
}

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

class _EStopButton extends ConsumerWidget {
  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return TextButton(
      onPressed: () {
        // Zero drive input immediately.
        ref.read(driveInputProvider.notifier).reset();
        ref.read(connectionProvider.notifier).setDriveInput(0, 0);
        // Send explicit emergency stop command.
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

class _BottomBar extends StatelessWidget {
  final bool connected;

  const _BottomBar({required this.connected});

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 36,
      color: Colors.black87,
      padding: const EdgeInsets.symmetric(horizontal: 12),
      child: Row(
        children: [
          // E-Stop button.
          _EStopButton(),
          const Spacer(),
          // Back button to exit drive mode.
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
