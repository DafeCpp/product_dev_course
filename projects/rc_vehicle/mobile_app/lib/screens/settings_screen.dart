import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// App-level settings (not vehicle settings).
/// Stored locally on the phone via SharedPreferences.

final appSettingsProvider =
    StateNotifierProvider<AppSettingsNotifier, AppSettings>(
  (ref) => AppSettingsNotifier(),
);

class AppSettings {
  final bool invertThrottle;
  final bool invertSteering;
  final double gyroSensitivityDeg;
  final double joystickDeadzone;
  final bool hapticFeedback;
  final bool darkTheme;
  final bool useKmh;
  final int udpPort;

  const AppSettings({
    this.invertThrottle = false,
    this.invertSteering = false,
    this.gyroSensitivityDeg = 30,
    this.joystickDeadzone = 0.05,
    this.hapticFeedback = true,
    this.darkTheme = true,
    this.useKmh = false,
    this.udpPort = 5555,
  });

  AppSettings copyWith({
    bool? invertThrottle,
    bool? invertSteering,
    double? gyroSensitivityDeg,
    double? joystickDeadzone,
    bool? hapticFeedback,
    bool? darkTheme,
    bool? useKmh,
    int? udpPort,
  }) {
    return AppSettings(
      invertThrottle: invertThrottle ?? this.invertThrottle,
      invertSteering: invertSteering ?? this.invertSteering,
      gyroSensitivityDeg: gyroSensitivityDeg ?? this.gyroSensitivityDeg,
      joystickDeadzone: joystickDeadzone ?? this.joystickDeadzone,
      hapticFeedback: hapticFeedback ?? this.hapticFeedback,
      darkTheme: darkTheme ?? this.darkTheme,
      useKmh: useKmh ?? this.useKmh,
      udpPort: udpPort ?? this.udpPort,
    );
  }
}

class AppSettingsNotifier extends StateNotifier<AppSettings> {
  AppSettingsNotifier() : super(const AppSettings()) {
    _load();
  }

  Future<void> _load() async {
    final p = await SharedPreferences.getInstance();
    state = AppSettings(
      invertThrottle: p.getBool('invertThrottle') ?? false,
      invertSteering: p.getBool('invertSteering') ?? false,
      gyroSensitivityDeg: p.getDouble('gyroSensitivityDeg') ?? 30,
      joystickDeadzone: p.getDouble('joystickDeadzone') ?? 0.05,
      hapticFeedback: p.getBool('hapticFeedback') ?? true,
      darkTheme: p.getBool('darkTheme') ?? true,
      useKmh: p.getBool('useKmh') ?? false,
      udpPort: p.getInt('udpPort') ?? 5555,
    );
  }

  Future<void> _save() async {
    final p = await SharedPreferences.getInstance();
    await p.setBool('invertThrottle', state.invertThrottle);
    await p.setBool('invertSteering', state.invertSteering);
    await p.setDouble('gyroSensitivityDeg', state.gyroSensitivityDeg);
    await p.setDouble('joystickDeadzone', state.joystickDeadzone);
    await p.setBool('hapticFeedback', state.hapticFeedback);
    await p.setBool('darkTheme', state.darkTheme);
    await p.setBool('useKmh', state.useKmh);
    await p.setInt('udpPort', state.udpPort);
  }

  void update(AppSettings Function(AppSettings) updater) {
    state = updater(state);
    _save();
  }
}

class SettingsScreen extends ConsumerWidget {
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = ref.watch(appSettingsProvider);
    final notifier = ref.read(appSettingsProvider.notifier);

    return Scaffold(
      appBar: AppBar(title: const Text('Settings')),
      body: ListView(
        children: [
          _SectionHeader('Controls'),
          SwitchListTile(
            title: const Text('Invert throttle'),
            subtitle: const Text('Forward = swipe down'),
            value: s.invertThrottle,
            onChanged: (v) =>
                notifier.update((s) => s.copyWith(invertThrottle: v)),
          ),
          SwitchListTile(
            title: const Text('Invert steering'),
            subtitle: const Text('Left = drag right'),
            value: s.invertSteering,
            onChanged: (v) =>
                notifier.update((s) => s.copyWith(invertSteering: v)),
          ),
          ListTile(
            title: const Text('Joystick deadzone'),
            subtitle: Slider(
              value: s.joystickDeadzone,
              min: 0,
              max: 0.15,
              divisions: 15,
              label: '${(s.joystickDeadzone * 100).round()}%',
              onChanged: (v) =>
                  notifier.update((s) => s.copyWith(joystickDeadzone: v)),
            ),
            trailing: Text('${(s.joystickDeadzone * 100).round()}%'),
          ),
          ListTile(
            title: const Text('Gyro sensitivity'),
            subtitle: Slider(
              value: s.gyroSensitivityDeg,
              min: 10,
              max: 60,
              divisions: 50,
              label: '${s.gyroSensitivityDeg.round()}°',
              onChanged: (v) =>
                  notifier.update((s) => s.copyWith(gyroSensitivityDeg: v)),
            ),
            trailing: Text('${s.gyroSensitivityDeg.round()}°'),
          ),

          _SectionHeader('Feedback'),
          SwitchListTile(
            title: const Text('Haptic feedback'),
            subtitle: const Text('Vibration on events'),
            value: s.hapticFeedback,
            onChanged: (v) =>
                notifier.update((s) => s.copyWith(hapticFeedback: v)),
          ),

          _SectionHeader('Display'),
          SwitchListTile(
            title: const Text('Dark theme'),
            value: s.darkTheme,
            onChanged: (v) =>
                notifier.update((s) => s.copyWith(darkTheme: v)),
          ),
          SwitchListTile(
            title: const Text('Speed in km/h'),
            subtitle: const Text('Instead of m/s'),
            value: s.useKmh,
            onChanged: (v) =>
                notifier.update((s) => s.copyWith(useKmh: v)),
          ),

          _SectionHeader('Network'),
          ListTile(
            title: const Text('UDP telemetry port'),
            trailing: SizedBox(
              width: 80,
              child: TextFormField(
                initialValue: s.udpPort.toString(),
                keyboardType: TextInputType.number,
                textAlign: TextAlign.end,
                decoration: const InputDecoration(
                  isDense: true,
                  border: OutlineInputBorder(),
                ),
                onFieldSubmitted: (v) {
                  final port = int.tryParse(v);
                  if (port != null && port > 1024 && port < 65536) {
                    notifier.update((s) => s.copyWith(udpPort: port));
                  }
                },
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _SectionHeader extends StatelessWidget {
  final String title;
  const _SectionHeader(this.title);

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 24, 16, 8),
      child: Text(
        title,
        style: Theme.of(context).textTheme.titleSmall?.copyWith(
              color: Theme.of(context).colorScheme.primary,
            ),
      ),
    );
  }
}
