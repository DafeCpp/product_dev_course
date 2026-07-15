import { useMemo } from "react";
import { EmptyState } from "../common";
import CaptureSessionTimeline from "../CaptureSessionTimeline";
import LiveSensorPanel from "../LiveSensorPanel";
import TelemetryPanel from "../TelemetryPanel";
import type { CaptureSession, Sensor } from "../../types";

interface LiveTelemetryWorkspaceProps {
  panelIds: string[];
  sensors: Sensor[];
  sensorsLoading: boolean;
  sensorsError: unknown;
  projectId: string;
  projectName?: string;
  sessions: CaptureSession[];
  recentValues: Record<string, number[]>;
  panelSizes: Record<string, { width: number; height: number }>;
  containerWidth: number;
  containerRef: React.RefObject<HTMLDivElement | null>;
  draggingId: string | null;
  dragOverId: string | null;
  onAddHistorySession: (id: string) => void;
  onRemove: (id: string) => void;
  onMove: (source: string, target: string) => void;
  onSizeChange: (id: string, size: { width: number; height: number }) => void;
  onRecordReceived: (id: string, value: number) => void;
  onDraggingChange: (id: string | null) => void;
  onDragOverChange: (id: string | null) => void;
}
export default function LiveTelemetryWorkspace(
  props: LiveTelemetryWorkspaceProps,
) {
  const title = props.projectName ? `Панель: ${props.projectName}` : "Панель";
  const error = props.sensorsError
    ? typeof props.sensorsError === "string"
      ? props.sensorsError
      : (props.sensorsError as Error).message
    : null;
  const panels = useMemo(
    () =>
      props.panelIds.map((id, index) => ({
        id,
        index,
        wide: Boolean(
          props.panelSizes[id] &&
          props.containerWidth > 0 &&
          props.panelSizes[id].width > props.containerWidth / 2,
        ),
      })),
    [props.containerWidth, props.panelIds, props.panelSizes],
  );
  return (
    <div className="telemetry-view__live-layout">
      <section className="telemetry-view__live-stage card detail-card">
        <div className="detail-section-header">
          <div className="detail-section-header__copy">
            <span className="detail-card__eyebrow">Live Canvas</span>
            <h3 className="detail-card__title">Панели live-телеметрии</h3>
            <p>
              Добавляйте независимые панели, меняйте их порядок и отслеживайте
              несколько сенсоров параллельно.
            </p>
          </div>
        </div>
        {props.sessions.length > 0 && (
          <CaptureSessionTimeline
            sessions={props.sessions}
            onSessionClick={props.onAddHistorySession}
          />
        )}
        {!panels.length ? (
          <EmptyState message="Добавьте панель, чтобы начать просмотр графиков." />
        ) : (
          <div className="telemetry-view__panels" ref={props.containerRef}>
            {panels.map(({ id, index, wide }) => (
              <div
                key={id}
                className={`telemetry-view__panel-item${props.draggingId === id ? " telemetry-view__panel-item--dragging" : ""}${props.dragOverId === id ? " telemetry-view__panel-item--over" : ""}${wide ? " telemetry-view__panel-item--full" : ""}`}
                onDragOver={(event) => {
                  if (!props.draggingId || props.draggingId === id) return;
                  event.preventDefault();
                  event.dataTransfer.dropEffect = "move";
                  props.onDragOverChange(id);
                }}
                onDragLeave={(event) => {
                  if (
                    !event.currentTarget.contains(event.relatedTarget as Node)
                  )
                    props.onDragOverChange(null);
                }}
                onDrop={(event) => {
                  event.preventDefault();
                  if (props.draggingId) props.onMove(props.draggingId, id);
                  props.onDraggingChange(null);
                  props.onDragOverChange(null);
                }}
              >
                <TelemetryPanel
                  panelId={id}
                  sensors={props.sensors}
                  sensorsLoading={props.sensorsLoading}
                  sensorsError={error}
                  title={`${title} #${index + 1}`}
                  onRemove={() => props.onRemove(id)}
                  onSizeChange={(size) => props.onSizeChange(id, size)}
                  onRecordReceived={props.onRecordReceived}
                  dragHandleProps={{
                    draggable: true,
                    onDragStart: (event) => {
                      props.onDraggingChange(id);
                      event.dataTransfer.effectAllowed = "move";
                      event.dataTransfer.setData("text/plain", id);
                    },
                    onDragEnd: () => {
                      props.onDraggingChange(null);
                      props.onDragOverChange(null);
                    },
                  }}
                />
              </div>
            ))}
          </div>
        )}
      </section>
      {props.projectId && props.sensors.length > 0 && (
        <LiveSensorPanel
          sensors={props.sensors}
          projectId={props.projectId}
          recentValues={props.recentValues}
        />
      )}
    </div>
  );
}
