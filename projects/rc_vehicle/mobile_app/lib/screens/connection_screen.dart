import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../models/connection_state.dart';
import '../providers/connection_provider.dart';
import '../services/wifi_discovery.dart';
import 'main_screen.dart';

class ConnectionScreen extends ConsumerStatefulWidget {
  const ConnectionScreen({super.key});

  @override
  ConsumerState<ConnectionScreen> createState() => _ConnectionScreenState();
}

class _ConnectionScreenState extends ConsumerState<ConnectionScreen> {
  final _ipController = TextEditingController();
  List<String> _history = [];
  bool _manualEntry = false;

  DiscoveredNetwork? _discovered;
  bool _scanning = false;
  bool _probing = false;
  String? _probeResult;

  @override
  void initState() {
    super.initState();
    _loadHistory();
    _startDiscovery();
  }

  Future<void> _loadHistory() async {
    final history =
        await ref.read(connectionProvider.notifier).loadIpHistory();
    if (mounted) setState(() => _history = history);
  }

  Future<void> _startDiscovery() async {
    setState(() {
      _scanning = true;
      _discovered = null;
      _probeResult = null;
    });

    final hasPermission = await WifiDiscovery.hasPermission();
    if (!hasPermission) {
      final granted = await WifiDiscovery.requestPermission();
      if (!granted) {
        if (mounted) {
          setState(() {
            _scanning = false;
            _probeResult = 'Location permission needed for WiFi detection';
          });
        }
        return;
      }
    }

    final network = await WifiDiscovery.discover();
    if (!mounted) return;

    setState(() {
      _discovered = network;
      _scanning = false;
    });

    if (network != null && network.isEspNetwork) {
      // Auto-probe the gateway.
      _probeGateway(network.gatewayIp);
    }
  }

  Future<void> _probeGateway(String ip) async {
    setState(() {
      _probing = true;
      _probeResult = null;
    });

    final found = await WifiDiscovery.probeEsp(ip);
    if (!mounted) return;

    setState(() {
      _probing = false;
      _probeResult =
          found ? 'ESP32 found at $ip' : 'No ESP32 response at $ip';
    });

    // Auto-connect if ESP32 confirmed.
    if (found) {
      _connect(ip);
    }
  }

  void _connect(String ip) {
    ref.read(connectionProvider.notifier).connect(ip);
  }

  @override
  void dispose() {
    _ipController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final conn = ref.watch(connectionProvider);

    ref.listen<VehicleConnectionState>(connectionProvider, (prev, next) {
      if (next.isConnected && mounted) {
        Navigator.of(context).pushReplacement(
          MaterialPageRoute(builder: (_) => const MainScreen()),
        );
      }
    });

    return Scaffold(
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const SizedBox(height: 48),
              Icon(
                Icons.directions_car,
                size: 72,
                color: Theme.of(context).colorScheme.primary,
              ),
              const SizedBox(height: 16),
              Text(
                'RC Vehicle',
                style: Theme.of(context).textTheme.headlineMedium,
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 32),

              // --- Auto-discovery section ---
              _buildDiscoverySection(context, conn),

              const SizedBox(height: 12),

              // Quick connect to AP
              _ConnectTile(
                ip: '192.168.4.1',
                label: 'AP Mode (default)',
                onTap: () => _connect('192.168.4.1'),
                isLoading:
                    conn.status == VehicleConnectionStatus.connecting &&
                    conn.ipAddress == '192.168.4.1',
              ),

              const SizedBox(height: 12),

              // History
              if (_history
                  .where((h) => h != '192.168.4.1')
                  .isNotEmpty) ...[
                Text(
                  'Recent',
                  style: Theme.of(context).textTheme.titleSmall,
                ),
                const SizedBox(height: 8),
                for (final ip
                    in _history.where((h) => h != '192.168.4.1'))
                  Padding(
                    padding: const EdgeInsets.only(bottom: 4),
                    child: _ConnectTile(
                      ip: ip,
                      onTap: () => _connect(ip),
                      isLoading:
                          conn.status == VehicleConnectionStatus.connecting &&
                          conn.ipAddress == ip,
                    ),
                  ),
                const SizedBox(height: 12),
              ],

              // Manual IP entry
              if (_manualEntry) ...[
                TextField(
                  controller: _ipController,
                  decoration: const InputDecoration(
                    labelText: 'IP Address',
                    hintText: '192.168.1.42',
                    border: OutlineInputBorder(),
                  ),
                  keyboardType: TextInputType.number,
                  onSubmitted: (v) {
                    if (v.isNotEmpty) _connect(v);
                  },
                ),
                const SizedBox(height: 8),
                FilledButton(
                  onPressed: () {
                    final ip = _ipController.text.trim();
                    if (ip.isNotEmpty) _connect(ip);
                  },
                  child: const Text('Connect'),
                ),
              ] else
                OutlinedButton(
                  onPressed: () => setState(() => _manualEntry = true),
                  child: const Text('Enter IP manually...'),
                ),

              const Spacer(),

              // Status
              if (conn.error != null)
                Card(
                  color: Theme.of(context).colorScheme.errorContainer,
                  child: Padding(
                    padding: const EdgeInsets.all(12),
                    child: Text(
                      conn.error!,
                      style: TextStyle(
                        color:
                            Theme.of(context).colorScheme.onErrorContainer,
                      ),
                    ),
                  ),
                ),

              if (conn.status == VehicleConnectionStatus.connecting)
                const Padding(
                  padding: EdgeInsets.all(16),
                  child: Center(child: CircularProgressIndicator()),
                ),

              Text(
                _statusText(conn.status),
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodySmall,
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildDiscoverySection(
    BuildContext context,
    VehicleConnectionState conn,
  ) {
    if (_scanning) {
      return Card(
        child: ListTile(
          leading: const SizedBox(
            width: 20,
            height: 20,
            child: CircularProgressIndicator(strokeWidth: 2),
          ),
          title: const Text('Scanning WiFi...'),
          subtitle: const Text('Looking for ESP32 network'),
        ),
      );
    }

    if (_discovered != null && _discovered!.isEspNetwork) {
      return Card(
        color: Theme.of(context).colorScheme.primaryContainer,
        child: ListTile(
          leading: Icon(
            _probing
                ? Icons.search
                : (_probeResult?.startsWith('ESP32 found') == true
                    ? Icons.check_circle
                    : Icons.wifi),
            color: Theme.of(context).colorScheme.onPrimaryContainer,
          ),
          title: Text(
            _discovered!.ssid,
            style: TextStyle(
              color: Theme.of(context).colorScheme.onPrimaryContainer,
            ),
          ),
          subtitle: Text(
            _probing
                ? 'Probing ${_discovered!.gatewayIp}...'
                : _probeResult ?? 'Gateway: ${_discovered!.gatewayIp}',
            style: TextStyle(
              color: Theme.of(context)
                  .colorScheme
                  .onPrimaryContainer
                  .withValues(alpha: 0.7),
            ),
          ),
          trailing: _probing
              ? const SizedBox(
                  width: 20,
                  height: 20,
                  child: CircularProgressIndicator(strokeWidth: 2),
                )
              : IconButton(
                  icon: const Icon(Icons.arrow_forward),
                  onPressed: () => _connect(_discovered!.gatewayIp),
                ),
          onTap: _probing
              ? null
              : () => _connect(_discovered!.gatewayIp),
        ),
      );
    }

    if (_discovered != null) {
      // Connected to WiFi but not an ESP network.
      return Card(
        child: ListTile(
          leading: const Icon(Icons.wifi, color: Colors.grey),
          title: Text(_discovered!.ssid),
          subtitle: const Text('Not an ESP32 network'),
          trailing: TextButton(
            onPressed: () => _probeGateway(_discovered!.gatewayIp),
            child: const Text('Try anyway'),
          ),
        ),
      );
    }

    // No WiFi or permission denied.
    return Card(
      child: ListTile(
        leading: const Icon(Icons.wifi_off, color: Colors.grey),
        title: const Text('No WiFi detected'),
        subtitle: Text(_probeResult ?? 'Connect to ESP32 WiFi network'),
        trailing: IconButton(
          icon: const Icon(Icons.refresh),
          onPressed: _startDiscovery,
        ),
      ),
    );
  }

  String _statusText(VehicleConnectionStatus s) {
    switch (s) {
      case VehicleConnectionStatus.disconnected:
        return 'Not connected';
      case VehicleConnectionStatus.connecting:
        return 'Connecting...';
      case VehicleConnectionStatus.connected:
        return 'Connected';
    }
  }
}

class _ConnectTile extends StatelessWidget {
  final String ip;
  final String? label;
  final VoidCallback onTap;
  final bool isLoading;

  const _ConnectTile({
    required this.ip,
    this.label,
    required this.onTap,
    this.isLoading = false,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      child: ListTile(
        title: Text(ip),
        subtitle: label != null ? Text(label!) : null,
        trailing: isLoading
            ? const SizedBox(
                width: 24,
                height: 24,
                child: CircularProgressIndicator(strokeWidth: 2),
              )
            : const Icon(Icons.arrow_forward),
        onTap: isLoading ? null : onTap,
      ),
    );
  }
}
