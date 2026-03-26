import 'dart:collection';
import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

class MiniChartData {
  final Queue<double> _data;
  final int capacity;
  final Color color;

  MiniChartData({required this.color, this.capacity = 200}) : _data = Queue();

  void add(double value) {
    _data.addLast(value);
    if (_data.length > capacity) _data.removeFirst();
  }

  List<FlSpot> toSpots() {
    final list = _data.toList();
    return List.generate(list.length, (i) => FlSpot(i.toDouble(), list[i]));
  }

  void clear() => _data.clear();
  bool get isEmpty => _data.isEmpty;
}

class MiniChart extends StatelessWidget {
  final List<MiniChartData> series;
  final double? minY;
  final double? maxY;
  final double height;

  const MiniChart({
    super.key,
    required this.series,
    this.minY,
    this.maxY,
    this.height = 40,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: height,
      child: LineChart(
        LineChartData(
          lineBarsData: series
              .map((s) => LineChartBarData(
                    spots: s.toSpots(),
                    isCurved: true,
                    color: s.color.withValues(alpha: 0.8),
                    barWidth: 1.2,
                    dotData: const FlDotData(show: false),
                    belowBarData: BarAreaData(
                      show: true,
                      color: s.color.withValues(alpha: 0.08),
                    ),
                  ))
              .toList(),
          titlesData: const FlTitlesData(show: false),
          gridData: const FlGridData(show: false),
          borderData: FlBorderData(show: false),
          minY: minY,
          maxY: maxY,
          lineTouchData: const LineTouchData(enabled: false),
          clipData: const FlClipData.all(),
        ),
        duration: Duration.zero,
      ),
    );
  }
}
