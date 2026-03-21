import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/connection_provider.dart';
import '../tabs/drive_tab.dart';
import '../tabs/setup_tab.dart';
import '../tabs/diagnostics_tab.dart';
import '../tabs/telemetry_tab.dart';
import 'connection_screen.dart';
import 'settings_screen.dart';

class MainScreen extends ConsumerStatefulWidget {
  const MainScreen({super.key});

  @override
  ConsumerState<MainScreen> createState() => _MainScreenState();
}

class _MainScreenState extends ConsumerState<MainScreen> {
  int _tabIndex = 0;

  @override
  Widget build(BuildContext context) {
    ref.watch(connectionProvider);

    ref.listen(connectionProvider, (prev, next) {
      if (!next.isConnected && prev?.isConnected == true) {
        Navigator.of(context).pushAndRemoveUntil(
          MaterialPageRoute(builder: (_) => const ConnectionScreen()),
          (_) => false,
        );
      }
    });

    return Scaffold(
      appBar: _tabIndex == 0
          ? null
          : AppBar(
              title: Text(_tabTitle()),
              actions: [
                IconButton(
                  icon: const Icon(Icons.settings),
                  onPressed: () => Navigator.of(context).push(
                    MaterialPageRoute(
                      builder: (_) => const SettingsScreen(),
                    ),
                  ),
                ),
              ],
            ),
      body: IndexedStack(
        index: _tabIndex,
        children: const [
          DriveTab(),
          TelemetryTab(),
          SetupTab(),
          DiagnosticsTab(),
        ],
      ),
      bottomNavigationBar: _tabIndex == 0
          ? null
          : NavigationBar(
              selectedIndex: _tabIndex,
              onDestinationSelected: (i) => setState(() => _tabIndex = i),
              destinations: const [
                NavigationDestination(
                  icon: Icon(Icons.gamepad),
                  label: 'Drive',
                ),
                NavigationDestination(
                  icon: Icon(Icons.show_chart),
                  label: 'Telemetry',
                ),
                NavigationDestination(
                  icon: Icon(Icons.tune),
                  label: 'Setup',
                ),
                NavigationDestination(
                  icon: Icon(Icons.build),
                  label: 'Diagnostics',
                ),
              ],
            ),
    );
  }

  String _tabTitle() {
    switch (_tabIndex) {
      case 1:
        return 'Telemetry';
      case 2:
        return 'Setup';
      case 3:
        return 'Diagnostics';
      default:
        return '';
    }
  }
}
