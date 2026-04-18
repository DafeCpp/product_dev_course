import 'dart:async';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../models/stab_config.dart';
import '../providers/connection_provider.dart';

final stabConfigProvider =
    StateNotifierProvider<StabConfigNotifier, StabConfig?>(
  (ref) => StabConfigNotifier(ref),
);

class StabConfigNotifier extends StateNotifier<StabConfig?> {
  final Ref _ref;
  StreamSubscription? _sub;

  StabConfigNotifier(this._ref) : super(null) {
    _sub = _ref.read(connectionProvider.notifier).messages.listen(_onMessage);
  }

  void _onMessage(Map<String, dynamic> json) {
    final type = json['type'] as String?;
    if (type == 'stab_config' || type == 'set_stab_config_ack') {
      state = StabConfig.fromJson(json);
    }
  }

  void fetch() {
    _ref.read(connectionProvider.notifier).sendCommand({
      'type': 'get_stab_config',
    });
  }

  void apply(StabConfig config) {
    state = config;
    _ref.read(connectionProvider.notifier).sendCommand(config.toJson());
  }

  void setMode(DriveMode mode) {
    final c = state ?? StabConfig();
    apply(c.copyWith(mode: mode));
  }

  void setKidsPreset(int presetId) {
    _ref.read(connectionProvider.notifier).sendCommand({
      'type': 'set_kids_preset',
      'preset': presetId,
    });
  }

  void toggleKidsMode(bool active) {
    _ref.read(connectionProvider.notifier).sendCommand({
      'type': 'toggle_kids_mode',
      'active': active,
    });
  }

  void saveToNvs() {
    _ref.read(connectionProvider.notifier).sendCommand({
      'type': 'save_config',
    });
  }

  void loadFromNvs() {
    _ref.read(connectionProvider.notifier).sendCommand({
      'type': 'load_config',
    });
  }

  void resetToDefaults() {
    _ref.read(connectionProvider.notifier).sendCommand({
      'type': 'reset_config',
    });
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }
}
