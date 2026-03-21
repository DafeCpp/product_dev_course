import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/connection_provider.dart';
import '../services/rest_client.dart';

class DiagnosticsTab extends ConsumerStatefulWidget {
  const DiagnosticsTab({super.key});

  @override
  ConsumerState<DiagnosticsTab> createState() => _DiagnosticsTabState();
}

class _DiagnosticsTabState extends ConsumerState<DiagnosticsTab> {
  // Self-test state
  List<Map<String, dynamic>>? _testResults;
  bool? _testPassed;
  bool _testRunning = false;

  // WiFi state
  Map<String, dynamic>? _wifiStatus;
  List<Map<String, dynamic>>? _networks;
  bool _scanning = false;
  bool _wifiLoading = false;

  StreamSubscription? _msgSub;
  RestClient? _restClient;

  @override
  void initState() {
    super.initState();
    _msgSub = ref
        .read(connectionProvider.notifier)
        .messages
        .listen(_onWsMessage);
    _loadWifiStatus();
  }

  @override
  void dispose() {
    _msgSub?.cancel();
    super.dispose();
  }

  void _onWsMessage(Map<String, dynamic> json) {
    final type = json['type'] as String?;
    if (type == 'self_test_result') {
      setState(() {
        _testRunning = false;
        _testPassed = json['passed'] as bool? ?? false;
        _testResults =
            (json['tests'] as List<dynamic>?)?.cast<Map<String, dynamic>>() ??
                [];
      });
    }
  }

  RestClient get _rest {
    final ip = ref.read(connectionProvider).ipAddress;
    if (_restClient == null || _restClient!.baseUrl != 'http://$ip') {
      _restClient = RestClient(ip);
    }
    return _restClient!;
  }

  Future<void> _loadWifiStatus() async {
    setState(() => _wifiLoading = true);
    try {
      final status = await _rest.getWifiStatus();
      if (mounted) setState(() => _wifiStatus = status);
    } catch (_) {}
    if (mounted) setState(() => _wifiLoading = false);
  }

  Future<void> _scanNetworks() async {
    setState(() => _scanning = true);
    try {
      final nets = await _rest.scanNetworks();
      if (mounted) setState(() => _networks = nets);
    } catch (_) {}
    if (mounted) setState(() => _scanning = false);
  }

  void _runSelfTest() {
    setState(() {
      _testRunning = true;
      _testResults = null;
      _testPassed = null;
    });
    ref.read(connectionProvider.notifier).sendCommand({
      'type': 'run_self_test',
    });
  }

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _buildSelfTestSection(context),
        const SizedBox(height: 16),
        _buildWifiSection(context),
        const SizedBox(height: 16),
        _buildSystemInfoSection(context),
      ],
    );
  }

  // --- Self-Test ---

  Widget _buildSelfTestSection(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Text(
                  'Self-Test',
                  style: Theme.of(context).textTheme.titleMedium,
                ),
                const Spacer(),
                if (_testPassed != null)
                  Chip(
                    label: Text(_testPassed! ? 'PASS' : 'FAIL'),
                    backgroundColor: _testPassed!
                        ? Colors.green.withValues(alpha: 0.2)
                        : Colors.red.withValues(alpha: 0.2),
                  ),
              ],
            ),
            const SizedBox(height: 12),
            SizedBox(
              width: double.infinity,
              child: FilledButton.icon(
                icon: _testRunning
                    ? const SizedBox(
                        width: 18,
                        height: 18,
                        child: CircularProgressIndicator(
                          strokeWidth: 2,
                          color: Colors.white,
                        ),
                      )
                    : const Icon(Icons.play_arrow, size: 18),
                label: Text(_testRunning ? 'Running...' : 'Run Diagnostics'),
                onPressed: _testRunning ? null : _runSelfTest,
              ),
            ),
            if (_testResults != null) ...[
              const SizedBox(height: 12),
              for (final test in _testResults!)
                _TestResultRow(
                  name: test['name'] as String? ?? '',
                  passed: test['passed'] as bool? ?? false,
                  detail: test['detail'] as String?,
                ),
            ],
          ],
        ),
      ),
    );
  }

  // --- WiFi STA ---

  Widget _buildWifiSection(BuildContext context) {
    final sta = _wifiStatus?['sta'] as Map<String, dynamic>?;
    final staConnected = sta?['connected'] as bool? ?? false;
    final staSsid = sta?['ssid'] as String? ?? '';
    final staIp = sta?['ip'] as String? ?? '';

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Wi-Fi STA', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 8),

            // Current STA status
            if (_wifiLoading)
              const Center(child: CircularProgressIndicator())
            else if (staConnected) ...[
              ListTile(
                leading: const Icon(Icons.wifi, color: Colors.green),
                title: Text(staSsid),
                subtitle: Text('IP: $staIp'),
                trailing: TextButton(
                  onPressed: () async {
                    await _rest.disconnectSta();
                    _loadWifiStatus();
                  },
                  child: const Text('Disconnect'),
                ),
                contentPadding: EdgeInsets.zero,
              ),
            ] else
              const ListTile(
                leading: Icon(Icons.wifi_off, color: Colors.grey),
                title: Text('Not connected to any network'),
                contentPadding: EdgeInsets.zero,
              ),

            const Divider(),

            // Scan
            Row(
              children: [
                Text(
                  'Available Networks',
                  style: Theme.of(context).textTheme.titleSmall,
                ),
                const Spacer(),
                TextButton.icon(
                  icon: _scanning
                      ? const SizedBox(
                          width: 16,
                          height: 16,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.refresh, size: 18),
                  label: const Text('Scan'),
                  onPressed: _scanning ? null : _scanNetworks,
                ),
              ],
            ),

            if (_networks != null)
              for (final net in _networks!)
                ListTile(
                  title: Text(net['ssid'] as String? ?? '(hidden)'),
                  subtitle: Text(
                    'Ch ${net['channel']}  '
                    '${net['rssi']} dBm  '
                    '${(net['open'] as bool? ?? false) ? 'Open' : 'Secured'}',
                  ),
                  trailing: const Icon(Icons.arrow_forward_ios, size: 16),
                  onTap: () => _showConnectDialog(
                    net['ssid'] as String? ?? '',
                    net['open'] as bool? ?? false,
                  ),
                  contentPadding: EdgeInsets.zero,
                  dense: true,
                ),
          ],
        ),
      ),
    );
  }

  Future<void> _showConnectDialog(String ssid, bool isOpen) async {
    final passwordController = TextEditingController();
    final result = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Connect to $ssid'),
        content: isOpen
            ? const Text('This network is open. Connect without password?')
            : TextField(
                controller: passwordController,
                decoration: const InputDecoration(
                  labelText: 'Password',
                  border: OutlineInputBorder(),
                ),
                obscureText: true,
                autofocus: true,
              ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Connect'),
          ),
        ],
      ),
    );

    if (result == true && mounted) {
      setState(() => _wifiLoading = true);
      try {
        await _rest.connectSta(ssid, passwordController.text);
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: Text(
                'Connecting to $ssid... '
                'ESP32 will get a new IP. '
                'Connect your phone to the same network.',
              ),
              duration: const Duration(seconds: 5),
            ),
          );
        }
      } catch (e) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('Error: $e')),
          );
        }
      }
      _loadWifiStatus();
    }
    passwordController.dispose();
  }

  // --- System Info ---

  Widget _buildSystemInfoSection(BuildContext context) {
    final conn = ref.watch(connectionProvider);

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'System Info',
              style: Theme.of(context).textTheme.titleMedium,
            ),
            const SizedBox(height: 8),
            _InfoRow('ESP32 IP', conn.ipAddress),
            _InfoRow(
              'Connection',
              conn.isConnected ? 'WebSocket connected' : 'Disconnected',
            ),
            const _InfoRow('Protocol', 'JSON over WebSocket'),
          ],
        ),
      ),
    );
  }
}

class _TestResultRow extends StatelessWidget {
  final String name;
  final bool passed;
  final String? detail;

  const _TestResultRow({
    required this.name,
    required this.passed,
    this.detail,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        children: [
          Icon(
            passed ? Icons.check_circle : Icons.cancel,
            size: 18,
            color: passed ? Colors.green : Colors.red,
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              name,
              style: Theme.of(context).textTheme.bodyMedium,
            ),
          ),
          if (detail != null)
            Text(
              detail!,
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: Colors.white54,
                  ),
            ),
        ],
      ),
    );
  }
}

class _InfoRow extends StatelessWidget {
  final String label;
  final String value;
  const _InfoRow(this.label, this.value);

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          SizedBox(
            width: 120,
            child: Text(
              label,
              style: Theme.of(context).textTheme.bodySmall,
            ),
          ),
          Expanded(
            child: Text(value, style: Theme.of(context).textTheme.bodyMedium),
          ),
        ],
      ),
    );
  }
}
