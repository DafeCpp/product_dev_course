import 'dart:math';
import 'package:flutter/material.dart';

/// Single-axis horizontal joystick for steering (-1..+1).
/// Touch and drag left/right. Release = snap to center.
class SteeringJoystick extends StatefulWidget {
  final ValueChanged<double> onChanged;
  final double deadzone;

  const SteeringJoystick({
    super.key,
    required this.onChanged,
    this.deadzone = 0.05,
  });

  @override
  State<SteeringJoystick> createState() => _SteeringJoystickState();
}

class _SteeringJoystickState extends State<SteeringJoystick> {
  double _value = 0;
  Offset? _touchStart;

  void _onPanStart(DragStartDetails d) {
    _touchStart = d.localPosition;
  }

  void _onPanUpdate(DragUpdateDetails d, double width) {
    if (_touchStart == null) return;
    final maxDrag = width / 2 - 30; // knob radius margin
    final dx = d.localPosition.dx - _touchStart!.dx;
    final raw = (dx / maxDrag).clamp(-1.0, 1.0);
    final v = raw.abs() < widget.deadzone ? 0.0 : raw;
    if (v != _value) {
      _value = v;
      widget.onChanged(v);
      setState(() {});
    }
  }

  void _release() {
    _value = 0;
    _touchStart = null;
    widget.onChanged(0);
    setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, constraints) {
      final w = constraints.maxWidth;
      final h = constraints.maxHeight;
      return GestureDetector(
        onPanStart: _onPanStart,
        onPanUpdate: (d) => _onPanUpdate(d, w),
        onPanEnd: (_) => _release(),
        onPanCancel: _release,
        child: CustomPaint(
          painter: _JoystickPainter(
            value: _value,
            color: Theme.of(context).colorScheme.primary,
            bgColor: Theme.of(context).colorScheme.surfaceContainerHighest,
          ),
          size: Size(w, h),
        ),
      );
    });
  }
}

class _JoystickPainter extends CustomPainter {
  final double value;
  final Color color;
  final Color bgColor;

  _JoystickPainter({
    required this.value,
    required this.color,
    required this.bgColor,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height / 2;
    final trackRadius = min(size.width, size.height) / 2 - 8;
    final knobRadius = trackRadius * 0.35;

    // Background track.
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Rect.fromCenter(
          center: Offset(cx, cy),
          width: size.width - 16,
          height: knobRadius * 2 + 16,
        ),
        Radius.circular(knobRadius + 8),
      ),
      Paint()..color = bgColor,
    );

    // Center line.
    canvas.drawLine(
      Offset(cx, cy - knobRadius - 4),
      Offset(cx, cy + knobRadius + 4),
      Paint()
        ..color = Colors.white24
        ..strokeWidth = 1,
    );

    // Knob position.
    final maxOffset = cx - knobRadius - 12;
    final knobX = cx + value * maxOffset;

    // Knob.
    final knobPaint = Paint()
      ..color = value.abs() > 0.01 ? color : color.withValues(alpha: 0.7);
    canvas.drawCircle(Offset(knobX, cy), knobRadius, knobPaint);

    // Value label.
    final pct = (value * 100).round();
    final label = pct == 0 ? '0' : '${pct > 0 ? "+" : ""}$pct%';
    final tp = TextPainter(
      text: TextSpan(
        text: label,
        style: const TextStyle(
          color: Colors.white,
          fontSize: 14,
          fontWeight: FontWeight.bold,
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(canvas, Offset(knobX - tp.width / 2, cy - tp.height / 2));

    // L/R labels.
    _drawLabel(canvas, 'L', Offset(20, cy));
    _drawLabel(canvas, 'R', Offset(size.width - 20, cy));
  }

  void _drawLabel(Canvas canvas, String text, Offset center) {
    final tp = TextPainter(
      text: TextSpan(
        text: text,
        style: TextStyle(
          color: Colors.white.withValues(alpha: 0.35),
          fontSize: 14,
          fontWeight: FontWeight.w600,
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(canvas, Offset(center.dx - tp.width / 2, center.dy - tp.height / 2));
  }

  @override
  bool shouldRepaint(_JoystickPainter old) => old.value != value;
}
