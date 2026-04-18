import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/connection_provider.dart';

class ConnectionIndicator extends ConsumerWidget {
  const ConnectionIndicator({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final conn = ref.watch(connectionProvider);
    final color = conn.isConnected ? Colors.green : Colors.red;
    final label = conn.isConnected ? 'Connected' : 'Disconnected';

    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: 10,
          height: 10,
          decoration: BoxDecoration(
            color: color,
            shape: BoxShape.circle,
          ),
        ),
        const SizedBox(width: 6),
        Text(
          label,
          style: TextStyle(color: color, fontSize: 12, fontWeight: FontWeight.w600),
        ),
      ],
    );
  }
}
