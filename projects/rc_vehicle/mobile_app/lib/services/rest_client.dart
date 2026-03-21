import 'dart:convert';
import 'package:http/http.dart' as http;

class RestClient {
  final String _baseUrl;

  RestClient(String ip) : _baseUrl = 'http://$ip';

  String get baseUrl => _baseUrl;

  Future<Map<String, dynamic>> getWifiStatus() async {
    return _get('/api/wifi/status');
  }

  Future<List<Map<String, dynamic>>> scanNetworks() async {
    final resp = await _get('/api/wifi/scan');
    final networks = resp['networks'] as List<dynamic>? ?? [];
    return networks.cast<Map<String, dynamic>>();
  }

  Future<Map<String, dynamic>> connectSta(
    String ssid,
    String password, {
    bool save = true,
  }) async {
    return _post('/api/wifi/sta/connect', {
      'ssid': ssid,
      'password': password,
      'save': save,
    });
  }

  Future<Map<String, dynamic>> disconnectSta({bool forget = false}) async {
    return _post('/api/wifi/sta/disconnect', {'forget': forget});
  }

  Future<Map<String, dynamic>> _get(String path) async {
    final resp = await http
        .get(Uri.parse('$_baseUrl$path'))
        .timeout(const Duration(seconds: 5));
    if (resp.statusCode != 200) {
      throw Exception('HTTP ${resp.statusCode}: ${resp.body}');
    }
    return jsonDecode(resp.body) as Map<String, dynamic>;
  }

  Future<Map<String, dynamic>> _post(
    String path,
    Map<String, dynamic> body,
  ) async {
    final resp = await http
        .post(
          Uri.parse('$_baseUrl$path'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode(body),
        )
        .timeout(const Duration(seconds: 10));
    if (resp.statusCode != 200) {
      throw Exception('HTTP ${resp.statusCode}: ${resp.body}');
    }
    return jsonDecode(resp.body) as Map<String, dynamic>;
  }
}
