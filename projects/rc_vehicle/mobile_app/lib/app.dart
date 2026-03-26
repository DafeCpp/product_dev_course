import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'screens/connection_screen.dart';
import 'screens/settings_screen.dart';

class RcVehicleApp extends ConsumerWidget {
  const RcVehicleApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final settings = ref.watch(appSettingsProvider);

    return MaterialApp(
      title: 'RC Vehicle',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorSchemeSeed: Colors.blue,
        brightness: settings.darkTheme ? Brightness.dark : Brightness.light,
        useMaterial3: true,
      ),
      home: const ConnectionScreen(),
    );
  }
}
