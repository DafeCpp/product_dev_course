import 'package:flutter/material.dart';
import 'screens/connection_screen.dart';

class RcVehicleApp extends StatelessWidget {
  const RcVehicleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'RC Vehicle',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorSchemeSeed: Colors.blue,
        brightness: Brightness.dark,
        useMaterial3: true,
      ),
      home: const ConnectionScreen(),
    );
  }
}
