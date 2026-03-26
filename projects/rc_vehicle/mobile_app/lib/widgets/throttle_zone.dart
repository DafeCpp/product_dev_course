import 'package:flutter/material.dart';

/// Vertical touch zone for throttle/brake input.
/// Touch above center = gas (0..+1), below center = brake (0..-1).
/// Release = instant return to 0.
class ThrottleZone extends StatefulWidget {
  final ValueChanged<double> onChanged;
  final double deadzone;

  const ThrottleZone({
    super.key,
    required this.onChanged,
    this.deadzone = 0.05,
  });

  @override
  State<ThrottleZone> createState() => _ThrottleZoneState();
}

class _ThrottleZoneState extends State<ThrottleZone> {
  double _value = 0;

  void _updateFromPosition(Offset localPosition, double height) {
    // Top of zone = +1 (full gas), bottom = -1 (full brake), center = 0.
    final center = height / 2;
    final raw = -(localPosition.dy - center) / center;
    final clamped = raw.clamp(-1.0, 1.0);
    final v = clamped.abs() < widget.deadzone ? 0.0 : clamped;
    if (v != _value) {
      _value = v;
      widget.onChanged(v);
      setState(() {});
    }
  }

  void _release() {
    _value = 0;
    widget.onChanged(0);
    setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;

    return LayoutBuilder(builder: (context, constraints) {
      final h = constraints.maxHeight;
      final w = constraints.maxWidth;
      final center = h / 2;
      // Bar height relative to value.
      final barHeight = (_value.abs() * center).clamp(0.0, center);

      return GestureDetector(
        onPanStart: (d) => _updateFromPosition(d.localPosition, h),
        onPanUpdate: (d) => _updateFromPosition(d.localPosition, h),
        onPanEnd: (_) => _release(),
        onPanCancel: _release,
        child: CustomPaint(
          painter: _ThrottleZonePainter(
            value: _value,
            barHeight: barHeight,
            centerY: center,
            width: w,
            gasColor: cs.primary,
            brakeColor: cs.error,
            bgColor: cs.surfaceContainerHighest,
          ),
          size: Size(w, h),
        ),
      );
    });
  }
}

class _ThrottleZonePainter extends CustomPainter {
  final double value;
  final double barHeight;
  final double centerY;
  final double width;
  final Color gasColor;
  final Color brakeColor;
  final Color bgColor;

  _ThrottleZonePainter({
    required this.value,
    required this.barHeight,
    required this.centerY,
    required this.width,
    required this.gasColor,
    required this.brakeColor,
    required this.bgColor,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final bgPaint = Paint()..color = bgColor;
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Rect.fromLTWH(0, 0, size.width, size.height),
        const Radius.circular(12),
      ),
      bgPaint,
    );

    // Center line.
    final linePaint = Paint()
      ..color = Colors.white24
      ..strokeWidth = 1;
    canvas.drawLine(
      Offset(8, centerY),
      Offset(size.width - 8, centerY),
      linePaint,
    );

    // Value bar.
    if (barHeight > 0) {
      final isGas = value > 0;
      final barPaint = Paint()
        ..color = (isGas ? gasColor : brakeColor).withValues(alpha: 0.7);
      final rect = isGas
          ? Rect.fromLTRB(4, centerY - barHeight, size.width - 4, centerY)
          : Rect.fromLTRB(4, centerY, size.width - 4, centerY + barHeight);
      canvas.drawRRect(
        RRect.fromRectAndRadius(rect, const Radius.circular(8)),
        barPaint,
      );
    }

    // Value label.
    final pct = (value * 100).round();
    final label = value > 0
        ? '+$pct%'
        : value < 0
            ? '$pct%'
            : '0';
    final tp = TextPainter(
      text: TextSpan(
        text: label,
        style: const TextStyle(
          color: Colors.white,
          fontSize: 18,
          fontWeight: FontWeight.bold,
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(canvas, Offset((width - tp.width) / 2, centerY - tp.height / 2));

    // Axis labels.
    _drawLabel(canvas, 'GAS', Offset(width / 2, 12));
    _drawLabel(canvas, 'BRAKE', Offset(width / 2, size.height - 24));
  }

  void _drawLabel(Canvas canvas, String text, Offset center) {
    final tp = TextPainter(
      text: TextSpan(
        text: text,
        style: TextStyle(
          color: Colors.white.withValues(alpha: 0.4),
          fontSize: 11,
          fontWeight: FontWeight.w600,
          letterSpacing: 2,
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(canvas, Offset(center.dx - tp.width / 2, center.dy));
  }

  @override
  bool shouldRepaint(_ThrottleZonePainter old) => old.value != value;
}
