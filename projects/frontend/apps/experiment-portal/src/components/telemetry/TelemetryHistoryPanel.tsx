import { useState } from "react";
import { ArrowRightIcon, ExportIcon, RefreshCwIcon } from "../common";
import CaptureSessionTimeline from "../CaptureSessionTimeline";
import type useTelemetryHistory from "../../hooks/useTelemetryHistory";
import type { CaptureSession, Sensor } from "../../types";
import HistorySensorControls from "./HistorySensorControls";
import TelemetryHistoryChart from "./TelemetryHistoryChart";
import TelemetryHistorySettings from "./TelemetryHistorySettings";

interface TelemetryHistoryPanelProps {
  history: ReturnType<typeof useTelemetryHistory>;
  sessions: CaptureSession[];
  sensors: Sensor[];
  selectedSession?: CaptureSession;
  runId: string;
  onContinueLive: () => void;
  onExport: () => void;
}
export default function TelemetryHistoryPanel({
  history,
  sessions,
  selectedSession,
  runId,
  onContinueLive,
  onExport,
}: TelemetryHistoryPanelProps) {
  const [timeRange, setTimeRange] = useState<[string, string] | null>(null);
  return (
    <section className="telemetry-view__history card detail-card">
      <div className="history-panel-header">
        <div className="history-panel-header__copy">
          <span className="detail-card__eyebrow">History</span>
          <h3 className="detail-card__title">
            {selectedSession
              ? `Capture #${selectedSession.ordinal_number}`
              : "Историческая выборка"}
          </h3>
          {selectedSession?.started_at && (
            <div className="history-panel-header__sub">
              {new Date(selectedSession.started_at).toLocaleString("ru-RU")}
              {selectedSession.stopped_at &&
                ` → ${new Date(selectedSession.stopped_at).toLocaleString("ru-RU")}`}
            </div>
          )}
        </div>
        <div className="history-panel-header__actions">
          <button
            type="button"
            className="pill-btn pill-btn--primary"
            onClick={history.load}
            disabled={history.loading || !history.captureSessionId}
          >
            <RefreshCwIcon />
            {history.loading ? "Загрузка…" : "Загрузить"}
          </button>
          <button
            type="button"
            className="pill-btn"
            onClick={onContinueLive}
            disabled={
              history.loading ||
              !history.effectiveSensorIds.length ||
              !history.lastTimestamp
            }
          >
            <ArrowRightIcon />
            Продолжить в live
          </button>
          <button
            type="button"
            className="pill-btn"
            onClick={onExport}
            disabled={!history.captureSessionId || !runId}
          >
            <ExportIcon />
            Экспорт…
          </button>
          <TelemetryHistorySettings
            includeLate={history.includeLate}
            useAggregated={history.useAggregated}
            order={history.order}
            valueMode={history.valueMode}
            maxPoints={history.maxPoints}
            onIncludeLateChange={history.setIncludeLate}
            onAggregatedChange={history.setUseAggregated}
            onOrderChange={history.setOrder}
            onValueModeChange={history.setValueMode}
            onMaxPointsChange={history.setMaxPoints}
          />
        </div>
      </div>
      <HistorySensorControls
        sensors={
          history.sensorById.size ? Array.from(history.sensorById.values()) : []
        }
        selectedIds={history.sensorIds}
        filteredSensors={history.filteredSensors}
        filter={history.filter}
        limitReached={history.sensorIds.length >= 50}
        onFilterChange={history.setFilter}
        onAdd={history.add}
        onAddAll={history.addAll}
        onClear={history.clear}
        onRemove={history.remove}
      />
      {(history.error || history.loadError) && (
        <div className="telemetry-view__error">
          {history.error || history.loadError}
        </div>
      )}
      {(history.loadedCount > 0 || history.wasTruncated) && (
        <div className="telemetry-view__history-summary">
          {history.useAggregated
            ? `Загружено бакетов (1m): ${history.loadedCount}`
            : `Загружено точек: ${history.loadedCount}`}
          {history.wasTruncated &&
            ` (показаны ${history.order === "desc" ? "последние" : "первые"} ${history.displayMaxPoints})`}
        </div>
      )}
      {sessions.length > 0 && (
        <CaptureSessionTimeline
          sessions={sessions}
          timeRange={timeRange}
          onSessionClick={history.setCaptureSessionId}
        />
      )}
      <TelemetryHistoryChart
        rawSeries={history.rawSeries}
        aggregatedSeries={history.aggregatedSeries}
        aggregated={history.useAggregated}
        loading={history.loading}
        hasData={history.hasData}
        loadedCount={history.loadedCount}
        maxPoints={history.displayMaxPoints}
        onTimeRangeChange={setTimeRange}
      />
    </section>
  );
}
