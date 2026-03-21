import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../models/stab_config.dart';
import '../providers/connection_provider.dart';
import '../providers/stab_config_provider.dart';

class SetupTab extends ConsumerStatefulWidget {
  const SetupTab({super.key});

  @override
  ConsumerState<SetupTab> createState() => _SetupTabState();
}

class _SetupTabState extends ConsumerState<SetupTab> {
  String? _calibStatus;
  StreamSubscription? _msgSub;

  @override
  void initState() {
    super.initState();
    // Fetch config on entry.
    Future.microtask(() {
      ref.read(stabConfigProvider.notifier).fetch();
    });
    _msgSub = ref
        .read(connectionProvider.notifier)
        .messages
        .listen(_onWsMessage);
  }

  @override
  void dispose() {
    _msgSub?.cancel();
    super.dispose();
  }

  void _onWsMessage(Map<String, dynamic> json) {
    final type = json['type'] as String?;
    if (type == 'calibrate_imu_ack' || type == 'calib_status') {
      setState(() {
        _calibStatus = json['status'] as String? ?? 'unknown';
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final config = ref.watch(stabConfigProvider);

    if (config == null) {
      return const Center(child: CircularProgressIndicator());
    }

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _DriveModeSection(config: config),
        const SizedBox(height: 16),
        _KidsModeSection(config: config),
        const SizedBox(height: 16),
        _TrimSection(config: config),
        const SizedBox(height: 16),
        _PidSection(config: config),
        const SizedBox(height: 16),
        _CalibrationSection(calibStatus: _calibStatus),
        const SizedBox(height: 16),
        _StorageSection(),
      ],
    );
  }
}

// --- Drive Mode ---

class _DriveModeSection extends ConsumerWidget {
  final StabConfig config;
  const _DriveModeSection({required this.config});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Drive Mode', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 12),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: DriveMode.values.map((mode) {
                final selected = config.mode == mode;
                return ChoiceChip(
                  label: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(mode.label),
                      Text(
                        mode.description,
                        style: Theme.of(context).textTheme.bodySmall,
                      ),
                    ],
                  ),
                  selected: selected,
                  onSelected: (_) {
                    ref.read(stabConfigProvider.notifier).setMode(mode);
                  },
                );
              }).toList(),
            ),
          ],
        ),
      ),
    );
  }
}

// --- Kids Mode ---

class _KidsModeSection extends ConsumerWidget {
  final StabConfig config;
  const _KidsModeSection({required this.config});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    if (config.mode != DriveMode.kids) return const SizedBox.shrink();

    final km = config.kidsMode;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Kids Mode Presets',
              style: Theme.of(context).textTheme.titleMedium,
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                _presetButton(context, ref, 1, 'Toddler', '3-5y'),
                const SizedBox(width: 8),
                _presetButton(context, ref, 2, 'Child', '6-9y'),
                const SizedBox(width: 8),
                _presetButton(context, ref, 3, 'Preteen', '10-12y'),
              ],
            ),
            const SizedBox(height: 16),
            _SliderRow(
              label: 'Throttle limit',
              value: km.throttleLimit,
              min: 0.1,
              max: 1.0,
              format: (v) => '${(v * 100).round()}%',
              onChanged: (v) {
                ref.read(stabConfigProvider.notifier).apply(
                  config.copyWith(kidsMode: km.copyWith(throttleLimit: v)),
                );
              },
            ),
            _SliderRow(
              label: 'Steering limit',
              value: km.steeringLimit,
              min: 0.3,
              max: 1.0,
              format: (v) => '${(v * 100).round()}%',
              onChanged: (v) {
                ref.read(stabConfigProvider.notifier).apply(
                  config.copyWith(kidsMode: km.copyWith(steeringLimit: v)),
                );
              },
            ),
            _SliderRow(
              label: 'Reverse limit',
              value: km.reverseLimit,
              min: 0.1,
              max: 1.0,
              format: (v) => '${(v * 100).round()}%',
              onChanged: (v) {
                ref.read(stabConfigProvider.notifier).apply(
                  config.copyWith(kidsMode: km.copyWith(reverseLimit: v)),
                );
              },
            ),
            SwitchListTile(
              title: const Text('Anti-spin protection'),
              value: km.antiSpinEnabled,
              onChanged: (v) {
                ref.read(stabConfigProvider.notifier).apply(
                  config.copyWith(kidsMode: km.copyWith(antiSpinEnabled: v)),
                );
              },
              contentPadding: EdgeInsets.zero,
              dense: true,
            ),
          ],
        ),
      ),
    );
  }

  Widget _presetButton(
    BuildContext context,
    WidgetRef ref,
    int id,
    String name,
    String ages,
  ) {
    return Expanded(
      child: OutlinedButton(
        onPressed: () =>
            ref.read(stabConfigProvider.notifier).setKidsPreset(id),
        child: Column(
          children: [
            Text(name),
            Text(ages, style: Theme.of(context).textTheme.bodySmall),
          ],
        ),
      ),
    );
  }
}

// --- Trim ---

class _TrimSection extends ConsumerWidget {
  final StabConfig config;
  const _TrimSection({required this.config});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Trim', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 8),
            _SliderRow(
              label: 'Steering trim',
              value: config.steeringTrim,
              min: -0.2,
              max: 0.2,
              format: (v) => v.toStringAsFixed(2),
              onChanged: (v) {
                ref.read(stabConfigProvider.notifier).apply(
                  config.copyWith(steeringTrim: v),
                );
              },
            ),
            _SliderRow(
              label: 'Throttle trim',
              value: config.throttleTrim,
              min: -0.2,
              max: 0.2,
              format: (v) => v.toStringAsFixed(2),
              onChanged: (v) {
                ref.read(stabConfigProvider.notifier).apply(
                  config.copyWith(throttleTrim: v),
                );
              },
            ),
          ],
        ),
      ),
    );
  }
}

// --- PID (Advanced, expandable) ---

class _PidSection extends ConsumerStatefulWidget {
  final StabConfig config;
  const _PidSection({required this.config});

  @override
  ConsumerState<_PidSection> createState() => _PidSectionState();
}

class _PidSectionState extends ConsumerState<_PidSection> {
  bool _expanded = false;
  Timer? _debounce;

  @override
  void dispose() {
    _debounce?.cancel();
    super.dispose();
  }

  void _applyDebounced(StabConfig config) {
    _debounce?.cancel();
    _debounce = Timer(const Duration(milliseconds: 200), () {
      ref.read(stabConfigProvider.notifier).apply(config);
    });
  }

  @override
  Widget build(BuildContext context) {
    final config = widget.config;
    final pid = config.yawRatePid;

    return Card(
      child: Column(
        children: [
          ListTile(
            title: const Text('PID Settings (Advanced)'),
            trailing: Icon(
              _expanded ? Icons.expand_less : Icons.expand_more,
            ),
            onTap: () => setState(() => _expanded = !_expanded),
          ),
          if (_expanded)
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Yaw Rate PID',
                    style: Theme.of(context).textTheme.titleSmall,
                  ),
                  _SliderRow(
                    label: 'Kp',
                    value: pid.kp,
                    min: 0,
                    max: 1.0,
                    format: (v) => v.toStringAsFixed(3),
                    onChanged: (v) {
                      _applyDebounced(
                        config.copyWith(yawRatePid: pid.copyWith(kp: v)),
                      );
                    },
                  ),
                  _SliderRow(
                    label: 'Ki',
                    value: pid.ki,
                    min: 0,
                    max: 0.5,
                    format: (v) => v.toStringAsFixed(3),
                    onChanged: (v) {
                      _applyDebounced(
                        config.copyWith(yawRatePid: pid.copyWith(ki: v)),
                      );
                    },
                  ),
                  _SliderRow(
                    label: 'Kd',
                    value: pid.kd,
                    min: 0,
                    max: 0.1,
                    format: (v) => v.toStringAsFixed(4),
                    onChanged: (v) {
                      _applyDebounced(
                        config.copyWith(yawRatePid: pid.copyWith(kd: v)),
                      );
                    },
                  ),
                  const Divider(),
                  SwitchListTile(
                    title: const Text('Adaptive PID'),
                    value: config.adaptiveEnabled,
                    onChanged: (v) {
                      ref.read(stabConfigProvider.notifier).apply(
                        config.copyWith(adaptiveEnabled: v),
                      );
                    },
                    contentPadding: EdgeInsets.zero,
                    dense: true,
                  ),
                  SwitchListTile(
                    title: const Text('Pitch compensation'),
                    value: config.pitchCompEnabled,
                    onChanged: (v) {
                      ref.read(stabConfigProvider.notifier).apply(
                        config.copyWith(pitchCompEnabled: v),
                      );
                    },
                    contentPadding: EdgeInsets.zero,
                    dense: true,
                  ),
                  SwitchListTile(
                    title: const Text('Oversteer protection'),
                    value: config.oversteerWarnEnabled,
                    onChanged: (v) {
                      ref.read(stabConfigProvider.notifier).apply(
                        config.copyWith(oversteerWarnEnabled: v),
                      );
                    },
                    contentPadding: EdgeInsets.zero,
                    dense: true,
                  ),
                ],
              ),
            ),
        ],
      ),
    );
  }
}

// --- IMU Calibration ---

class _CalibrationSection extends ConsumerWidget {
  final String? calibStatus;
  const _CalibrationSection({required this.calibStatus});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'IMU Calibration',
              style: Theme.of(context).textTheme.titleMedium,
            ),
            if (calibStatus != null) ...[
              const SizedBox(height: 8),
              Chip(
                label: Text('Status: $calibStatus'),
                backgroundColor: calibStatus == 'done'
                    ? Colors.green.withValues(alpha: 0.2)
                    : calibStatus == 'failed'
                        ? Colors.red.withValues(alpha: 0.2)
                        : null,
              ),
            ],
            const SizedBox(height: 12),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                _calibButton(ref, 'Gyro', 'gyro', 'Keep vehicle still'),
                _calibButton(
                  ref,
                  'Full',
                  'full',
                  'Keep vehicle still on flat surface',
                ),
                _calibButton(
                  ref,
                  'Forward',
                  'forward',
                  'Push vehicle straight forward',
                ),
                _calibButton(
                  ref,
                  'Auto-Forward',
                  'auto_forward',
                  'Vehicle drives forward automatically',
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _calibButton(
    WidgetRef ref,
    String label,
    String mode,
    String hint,
  ) {
    return Tooltip(
      message: hint,
      child: OutlinedButton.icon(
        icon: const Icon(Icons.sensors, size: 18),
        label: Text(label),
        onPressed: () {
          ref.read(connectionProvider.notifier).sendCommand({
            'type': 'calibrate_imu',
            'mode': mode,
          });
        },
      ),
    );
  }
}

// --- NVS Storage ---

class _StorageSection extends ConsumerWidget {
  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Storage', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: FilledButton.icon(
                    icon: const Icon(Icons.save, size: 18),
                    label: const Text('Save to NVS'),
                    onPressed: () =>
                        ref.read(stabConfigProvider.notifier).saveToNvs(),
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: OutlinedButton.icon(
                    icon: const Icon(Icons.download, size: 18),
                    label: const Text('Load from NVS'),
                    onPressed: () =>
                        ref.read(stabConfigProvider.notifier).loadFromNvs(),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 8),
            SizedBox(
              width: double.infinity,
              child: TextButton.icon(
                icon: const Icon(Icons.restore, size: 18),
                label: const Text('Reset to factory defaults'),
                onPressed: () {
                  showDialog(
                    context: context,
                    builder: (ctx) => AlertDialog(
                      title: const Text('Reset?'),
                      content: const Text(
                        'This will reset all stabilization settings to factory defaults.',
                      ),
                      actions: [
                        TextButton(
                          onPressed: () => Navigator.pop(ctx),
                          child: const Text('Cancel'),
                        ),
                        FilledButton(
                          onPressed: () {
                            ref
                                .read(stabConfigProvider.notifier)
                                .resetToDefaults();
                            Navigator.pop(ctx);
                          },
                          child: const Text('Reset'),
                        ),
                      ],
                    ),
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// --- Reusable slider row ---

class _SliderRow extends StatelessWidget {
  final String label;
  final double value;
  final double min;
  final double max;
  final String Function(double) format;
  final ValueChanged<double> onChanged;

  const _SliderRow({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.format,
    required this.onChanged,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        SizedBox(
          width: 120,
          child: Text(label, style: Theme.of(context).textTheme.bodySmall),
        ),
        Expanded(
          child: Slider(
            value: value.clamp(min, max),
            min: min,
            max: max,
            onChanged: onChanged,
          ),
        ),
        SizedBox(
          width: 56,
          child: Text(
            format(value),
            style: Theme.of(context).textTheme.bodySmall,
            textAlign: TextAlign.end,
          ),
        ),
      ],
    );
  }
}
