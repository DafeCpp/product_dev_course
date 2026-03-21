import 'dart:collection';
import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

class ChartSeries {
  final String label;
  final Color color;
  final Queue<double> _data;
  final int capacity;

  ChartSeries({
    required this.label,
    required this.color,
    this.capacity = 1000,
  }) : _data = Queue();

  void add(double value) {
    _data.addLast(value);
    if (_data.length > capacity) _data.removeFirst();
  }

  List<FlSpot> toSpots() {
    final list = _data.toList();
    return List.generate(
      list.length,
      (i) => FlSpot(i.toDouble(), list[i]),
    );
  }

  void clear() => _data.clear();
  int get length => _data.length;
}

class TelemetryChart extends StatelessWidget {
  final String title;
  final List<ChartSeries> series;
  final double? minY;
  final double? maxY;

  const TelemetryChart({
    super.key,
    required this.title,
    required this.series,
    this.minY,
    this.maxY,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.only(left: 4, bottom: 4),
          child: Row(
            children: [
              Text(
                title,
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      fontWeight: FontWeight.w600,
                    ),
              ),
              const SizedBox(width: 8),
              for (final s in series) ...[
                Container(
                  width: 10,
                  height: 3,
                  color: s.color,
                  margin: const EdgeInsets.only(left: 6),
                ),
                Padding(
                  padding: const EdgeInsets.only(left: 2),
                  child: Text(
                    s.label,
                    style: Theme.of(context).textTheme.labelSmall?.copyWith(
                          color: s.color,
                        ),
                  ),
                ),
              ],
            ],
          ),
        ),
        SizedBox(
          height: 100,
          child: LineChart(
            LineChartData(
              lineBarsData: series
                  .map(
                    (s) => LineChartBarData(
                      spots: s.toSpots(),
                      isCurved: true,
                      color: s.color,
                      barWidth: 1.5,
                      dotData: const FlDotData(show: false),
                      belowBarData: BarAreaData(show: false),
                    ),
                  )
                  .toList(),
              titlesData: const FlTitlesData(show: false),
              gridData: FlGridData(
                show: true,
                drawVerticalLine: false,
                horizontalInterval: _autoInterval(),
                getDrawingHorizontalLine: (_) => FlLine(
                  color: Colors.white10,
                  strokeWidth: 0.5,
                ),
              ),
              borderData: FlBorderData(show: false),
              minY: minY,
              maxY: maxY,
              lineTouchData: const LineTouchData(enabled: false),
              clipData: const FlClipData.all(),
            ),
            duration: Duration.zero,
          ),
        ),
      ],
    );
  }

  double? _autoInterval() {
    if (minY != null && maxY != null) {
      return (maxY! - minY!) / 4;
    }
    return null;
  }
}
