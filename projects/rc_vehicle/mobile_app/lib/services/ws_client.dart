import 'dart:async';
import 'dart:convert';
import 'package:web_socket_channel/web_socket_channel.dart';

typedef JsonHandler = void Function(Map<String, dynamic> json);

class WsClient {
  WebSocketChannel? _channel;
  StreamSubscription? _subscription;
  Timer? _sendTimer;
  Timer? _reconnectTimer;

  final void Function()? onConnected;
  final void Function()? onDisconnected;
  final JsonHandler? onTelemetry;
  final JsonHandler? onMessage;

  String _ip = '';
  double _throttle = 0;
  double _steering = 0;
  bool _disposed = false;
  bool _connected = false;
  int _reconnectAttempts = 0;
  static const int _maxReconnectAttempts = 10;

  WsClient({
    this.onConnected,
    this.onDisconnected,
    this.onTelemetry,
    this.onMessage,
  });

  bool get isConnected => _connected;
  String get ip => _ip;

  Future<bool> connect(String ip) async {
    _ip = ip;
    _reconnectAttempts = 0;
    return _doConnect();
  }

  Future<bool> _doConnect() async {
    disconnect(permanent: false);
    final uri = Uri.parse('ws://$_ip/ws');
    try {
      _channel = WebSocketChannel.connect(uri);
      await _channel!.ready;
    } catch (e) {
      _channel = null;
      return false;
    }

    _subscription = _channel!.stream.listen(
      _onData,
      onError: (_) => _handleDisconnect(),
      onDone: _handleDisconnect,
    );

    // Send throttle/steering at 50 Hz.
    _sendTimer = Timer.periodic(
      const Duration(milliseconds: 20),
      (_) => _sendDriveCommand(),
    );

    _reconnectAttempts = 0;
    _connected = true;
    onConnected?.call();
    return true;
  }

  void _onData(dynamic data) {
    if (data is! String) return;
    try {
      final json = jsonDecode(data) as Map<String, dynamic>;
      final type = json['type'] as String?;
      if (type == 'telemetry') {
        onTelemetry?.call(json);
      } else {
        onMessage?.call(json);
      }
    } catch (_) {}
  }

  void _sendDriveCommand() {
    if (_channel == null) return;
    final cmd = jsonEncode({
      'type': 'cmd',
      'throttle': _throttle,
      'steering': _steering,
    });
    try {
      _channel!.sink.add(cmd);
    } catch (_) {
      _handleDisconnect();
    }
  }

  void setDriveInput(double throttle, double steering) {
    _throttle = throttle.clamp(-1.0, 1.0);
    _steering = steering.clamp(-1.0, 1.0);
  }

  void sendJson(Map<String, dynamic> json) {
    if (_channel == null) return;
    try {
      _channel!.sink.add(jsonEncode(json));
    } catch (_) {}
  }

  void _handleDisconnect() {
    if (_disposed) return;
    _sendTimer?.cancel();
    _sendTimer = null;
    _subscription?.cancel();
    _subscription = null;
    try {
      _channel?.sink.close();
    } catch (_) {}
    _channel = null;
    _throttle = 0;
    _steering = 0;
    _connected = false;
    onDisconnected?.call();
    _scheduleReconnect();
  }

  void _scheduleReconnect() {
    if (_disposed || _ip.isEmpty) return;
    if (_reconnectAttempts >= _maxReconnectAttempts) return;

    _reconnectAttempts++;
    final delay = Duration(
      milliseconds: _reconnectDelay(_reconnectAttempts),
    );
    _reconnectTimer = Timer(delay, () async {
      if (_disposed) return;
      await _doConnect();
    });
  }

  int _reconnectDelay(int attempt) {
    // 500ms, 1s, 2s, 4s, 5s (capped)
    final ms = 500 * (1 << (attempt - 1));
    return ms.clamp(500, 5000);
  }

  void disconnect({bool permanent = true}) {
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _sendTimer?.cancel();
    _sendTimer = null;
    _subscription?.cancel();
    _subscription = null;
    try {
      _channel?.sink.close();
    } catch (_) {}
    _channel = null;
    _throttle = 0;
    _steering = 0;
    _connected = false;
    if (permanent) {
      _ip = '';
    }
  }

  void dispose() {
    _disposed = true;
    disconnect();
  }
}
