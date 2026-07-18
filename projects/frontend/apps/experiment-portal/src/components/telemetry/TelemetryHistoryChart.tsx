import { useEffect, useMemo, useRef } from "react";
import Plotly from "plotly.js-dist-min";

interface RawSeries {
  name: string;
  x: string[];
  y: Array<number | null>;
}
interface AggregatedSeries {
  name: string;
  x: string[];
  avg: Array<number | null>;
  min: Array<number | null>;
  max: Array<number | null>;
}
interface TelemetryHistoryChartProps {
  rawSeries: RawSeries[];
  aggregatedSeries: AggregatedSeries[];
  aggregated: boolean;
  loading: boolean;
  hasData: boolean;
  loadedCount: number;
  maxPoints: number;
  onTimeRangeChange: (range: [string, string] | null) => void;
}
const COLORS = [
  "#2563eb",
  "#16a34a",
  "#f97316",
  "#a855f7",
  "#06b6d4",
  "#e11d48",
];
export default function TelemetryHistoryChart({
  rawSeries,
  aggregatedSeries,
  aggregated,
  loading,
  hasData,
  loadedCount,
  maxPoints,
  onTimeRangeChange,
}: TelemetryHistoryChartProps) {
  const ref = useRef<HTMLDivElement>(null);
  const data = useMemo(
    () =>
      aggregated
        ? aggregatedSeries.flatMap((series, index) => {
            const color = COLORS[index % COLORS.length];
            return [
              {
                x: series.x,
                y: series.min,
                type: "scatter",
                mode: "lines",
                line: { color: "transparent", width: 0 },
                showlegend: false,
                hoverinfo: "skip",
              },
              {
                x: series.x,
                y: series.max,
                type: "scatter",
                mode: "lines",
                line: { color: "transparent", width: 0 },
                fill: "tonexty",
                fillcolor: `${color}26`,
                showlegend: false,
              },
              {
                x: series.x,
                y: series.avg,
                type: "scatter",
                mode: "lines",
                name: series.name,
                line: { color, width: 2 },
              },
            ];
          })
        : rawSeries.map((series, index) => ({
            x: series.x,
            y: series.y,
            type: "scattergl",
            mode: "lines",
            name: series.name,
            line: { color: COLORS[index % COLORS.length], width: 2 },
            hovertemplate: "%{x}<br>%{y:.3f}<extra></extra>",
          })),
    [aggregated, aggregatedSeries, rawSeries],
  );
  const layout = useMemo(
    () => ({
      autosize: true,
      margin: { l: 42, r: 14, t: 12, b: 24 },
      showlegend: true,
      paper_bgcolor: "rgba(0,0,0,0)",
      plot_bgcolor: "rgba(15, 23, 42, 0.04)",
      xaxis: {
        gridcolor: "rgba(15, 23, 42, 0.12)",
        tickfont: { size: 10, color: "#475569" },
      },
      yaxis: {
        gridcolor: "rgba(15, 23, 42, 0.12)",
        tickfont: { size: 10, color: "#475569" },
      },
    }),
    [],
  );
  useEffect(() => {
    if (ref.current)
      Plotly.react(ref.current, hasData ? data : [], layout, {
        responsive: true,
        displayModeBar: false,
        displaylogo: false,
      });
  }, [data, hasData, layout]);
  useEffect(() => {
    const element = ref.current;
    if (!element) return;
    const plot = element as HTMLDivElement & {
      on?: (
        event: string,
        handler: (event: Record<string, unknown>) => void,
      ) => void;
      removeAllListeners?: (event: string) => void;
    };
    const handler = (event: Record<string, unknown>) => {
      const range = event["xaxis.range"] as [string, string] | undefined;
      onTimeRangeChange(range || null);
    };
    plot.on?.("plotly_relayout", handler);
    return () => {
      plot.removeAllListeners?.("plotly_relayout");
      Plotly.purge(element);
    };
  }, [onTimeRangeChange]);
  return (
    <div className="telemetry-view__history-chart">
      <div ref={ref} className="telemetry-view__plotly" />
      {loading && (
        <div className="telemetry-view__history-loading">
          Загрузка истории…
          {loadedCount > 0 && (
            <span>
              {" "}
              {loadedCount} / {maxPoints}
            </span>
          )}
        </div>
      )}
      {!hasData && !loading && (
        <div className="telemetry-view__empty">
          Нет данных — нажмите «Загрузить»
        </div>
      )}
    </div>
  );
}
