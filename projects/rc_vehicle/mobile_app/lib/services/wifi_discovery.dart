import 'dart:io';
import 'package:network_info_plus/network_info_plus.dart';
import 'package:permission_handler/permission_handler.dart';

class DiscoveredNetwork {
  final String ssid;
  final String gatewayIp;
  final bool isEspNetwork;

  const DiscoveredNetwork({
    required this.ssid,
    required this.gatewayIp,
    required this.isEspNetwork,
  });
}

class WifiDiscovery {
  static final _networkInfo = NetworkInfo();

  static final _espPatterns = [
    RegExp(r'RC', caseSensitive: false),
    RegExp(r'ESP', caseSensitive: false),
    RegExp(r'vehicle', caseSensitive: false),
  ];

  static bool _matchesEsp(String ssid) {
    return _espPatterns.any((p) => p.hasMatch(ssid));
  }

  static String _gatewayFromIp(String wifiIp) {
    // For AP mode (192.168.4.x) -> 192.168.4.1
    // For STA mode (e.g. 192.168.1.x) -> 192.168.1.1
    final parts = wifiIp.split('.');
    if (parts.length == 4) {
      return '${parts[0]}.${parts[1]}.${parts[2]}.1';
    }
    return '192.168.4.1';
  }

  /// Request location permission (required for WiFi SSID on Android 8+).
  static Future<bool> requestPermission() async {
    if (!Platform.isAndroid) return true;
    final status = await Permission.location.request();
    return status.isGranted;
  }

  /// Check if location permission is granted.
  static Future<bool> hasPermission() async {
    if (!Platform.isAndroid) return true;
    return await Permission.location.isGranted;
  }

  /// Discover current WiFi network and check if it looks like an ESP32.
  static Future<DiscoveredNetwork?> discover() async {
    try {
      final ssid = await _networkInfo.getWifiName();
      final wifiIp = await _networkInfo.getWifiIP();

      if (ssid == null || ssid.isEmpty || wifiIp == null || wifiIp.isEmpty) {
        return null;
      }

      // Remove quotes that some platforms add around SSID.
      final cleanSsid = ssid.replaceAll('"', '');
      if (cleanSsid == '<unknown ssid>') return null;

      final gateway = await _networkInfo.getWifiGatewayIP() ??
          _gatewayFromIp(wifiIp);

      return DiscoveredNetwork(
        ssid: cleanSsid,
        gatewayIp: gateway,
        isEspNetwork: _matchesEsp(cleanSsid),
      );
    } catch (_) {
      return null;
    }
  }

  /// Probe an IP by attempting a quick HTTP request to check if ESP32 is there.
  static Future<bool> probeEsp(String ip) async {
    try {
      final client = HttpClient()
        ..connectionTimeout = const Duration(seconds: 2);
      final request = await client.getUrl(
        Uri.parse('http://$ip/api/wifi/status'),
      );
      final response = await request.close().timeout(
        const Duration(seconds: 2),
      );
      client.close(force: true);
      return response.statusCode == 200;
    } catch (_) {
      return false;
    }
  }
}
