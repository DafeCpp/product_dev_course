import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../models/connection_state.dart';
import '../providers/connection_provider.dart';
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

  @override
  void initState() {
    super.initState();
    _loadHistory();
  }

  Future<void> _loadHistory() async {
    final history = await ref.read(connectionProvider.notifier).loadIpHistory();
    if (mounted) setState(() => _history = history);
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
      if (next.isConnected) {
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
              const SizedBox(height: 48),

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
              if (_history.isNotEmpty) ...[
                Text(
                  'Recent',
                  style: Theme.of(context).textTheme.titleSmall,
                ),
                const SizedBox(height: 8),
                for (final ip in _history.where((h) => h != '192.168.4.1'))
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
                        color: Theme.of(context).colorScheme.onErrorContainer,
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
