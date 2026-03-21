import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/connection_provider.dart';
import '../tabs/drive_tab.dart';
import 'connection_screen.dart';

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
      if (!next.isConnected &&
          prev?.isConnected == true) {
        Navigator.of(context).pushAndRemoveUntil(
          MaterialPageRoute(builder: (_) => const ConnectionScreen()),
          (_) => false,
        );
      }
    });

    return Scaffold(
      body: IndexedStack(
        index: _tabIndex,
        children: const [
          DriveTab(),
          _PlaceholderTab(title: 'Telemetry'),
          _PlaceholderTab(title: 'Setup'),
          _PlaceholderTab(title: 'Diagnostics'),
        ],
      ),
      bottomNavigationBar: _tabIndex == 0
          ? null // Hide tab bar in DriveTab (fullscreen)
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
}

class _PlaceholderTab extends StatelessWidget {
  final String title;
  const _PlaceholderTab({required this.title});

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Text(title, style: Theme.of(context).textTheme.headlineMedium),
    );
  }
}
