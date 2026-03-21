import 'dart:async';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/connection_state.dart';
import '../models/telemetry_frame.dart';
import '../services/ws_client.dart';

final connectionProvider =
    StateNotifierProvider<ConnectionNotifier, VehicleConnectionState>(
  (ref) => ConnectionNotifier(ref),
);

final telemetryProvider = StateProvider<TelemetryFrame?>((ref) => null);

class ConnectionNotifier extends StateNotifier<VehicleConnectionState> {
  final Ref _ref;
  WsClient? _ws;
  final StreamController<Map<String, dynamic>> _messageController =
      StreamController.broadcast();

  ConnectionNotifier(this._ref) : super(const VehicleConnectionState());

  WsClient? get ws => _ws;
  Stream<Map<String, dynamic>> get messages => _messageController.stream;

  Future<void> connect(String ip) async {
    state = state.copyWith(
      status: VehicleConnectionStatus.connecting,
      ipAddress: ip,
    );

    _ws?.dispose();
    _ws = WsClient(
      onConnected: () {
        state = state.copyWith(status: VehicleConnectionStatus.connected);
        _saveIpHistory(ip);
      },
      onDisconnected: () {
        if (mounted) {
          state = state.copyWith(status: VehicleConnectionStatus.disconnected);
        }
      },
      onTelemetry: (json) {
        _ref.read(telemetryProvider.notifier).state =
            TelemetryFrame.fromJson(json);
      },
      onMessage: (json) {
        _messageController.add(json);
      },
    );

    final ok = await _ws!.connect(ip);
    if (!ok && mounted) {
      state = state.copyWith(
        status: VehicleConnectionStatus.disconnected,
        error: 'Could not connect to $ip',
      );
    }
  }

  void setDriveInput(double throttle, double steering) {
    _ws?.setDriveInput(throttle, steering);
  }

  void sendCommand(Map<String, dynamic> json) {
    _ws?.sendJson(json);
  }

  void disconnect() {
    _ws?.dispose();
    _ws = null;
    state = state.copyWith(status: VehicleConnectionStatus.disconnected);
  }

  Future<List<String>> loadIpHistory() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getStringList('ip_history') ?? [];
  }

  Future<void> _saveIpHistory(String ip) async {
    final prefs = await SharedPreferences.getInstance();
    final history = prefs.getStringList('ip_history') ?? [];
    history.remove(ip);
    history.insert(0, ip);
    if (history.length > 5) history.removeLast();
    await prefs.setStringList('ip_history', history);
  }

  @override
  void dispose() {
    _ws?.dispose();
    _messageController.close();
    super.dispose();
  }
}
