import { createPortal } from "react-dom";
import { useEffect, useState } from "react";
import { useQuery } from "@tanstack/react-query";
import {
  captureSessionsApi,
  experimentsApi,
  projectsApi,
  runsApi,
  sensorsApi,
} from "../api/client";
import {
  EmptyState,
  FloatingActionButton,
  Loading,
} from "../components/common";
import TelemetryExportModal from "../components/TelemetryExportModal";
import LiveTelemetryWorkspace from "../components/telemetry/LiveTelemetryWorkspace";
import TelemetryFilters from "../components/telemetry/TelemetryFilters";
import TelemetryHistoryPanel from "../components/telemetry/TelemetryHistoryPanel";
import useLiveTelemetryWorkspace from "../hooks/useLiveTelemetryWorkspace";
import useTelemetryHistory from "../hooks/useTelemetryHistory";
import useTelemetryViewerState from "../hooks/useTelemetryViewerState";
import { setActiveProjectId } from "../utils/activeProject";
import { generateUUID } from "../utils/uuid";
import type { CaptureSession, Sensor } from "../types";
import "./TelemetryViewer.scss";

export default function TelemetryViewer() {
  const viewer = useTelemetryViewerState();
  const live = useLiveTelemetryWorkspace();
  const [showExportModal, setShowExportModal] = useState(false);
  const projectsQuery = useQuery({
    queryKey: ["projects"],
    queryFn: () => projectsApi.list(),
  });
  const sensorsQuery = useQuery({
    queryKey: ["sensors", viewer.projectId],
    queryFn: () => listAllSensors(viewer.projectId),
    enabled: !!viewer.projectId,
  });
  const experimentsQuery = useQuery({
    queryKey: ["experiments", viewer.projectId],
    queryFn: () =>
      experimentsApi.list({ project_id: viewer.projectId, page_size: 100 }),
    enabled: !!viewer.projectId,
  });
  const runsQuery = useQuery({
    queryKey: ["runs", viewer.experimentId],
    queryFn: () => runsApi.list(viewer.experimentId, { page_size: 100 }),
    enabled: !!viewer.experimentId,
  });
  const sessionsQuery = useQuery({
    queryKey: ["capture-sessions", viewer.runId],
    queryFn: () => captureSessionsApi.list(viewer.runId, { page_size: 200 }),
    enabled: !!viewer.runId,
  });
  const sensors = sensorsQuery.data?.sensors || [];
  const history = useTelemetryHistory(sensors);
  const projects = projectsQuery.data?.projects || [];
  const experiments = experimentsQuery.data?.experiments || [];
  const runs = runsQuery.data?.runs || [];
  const sessions = sessionsQuery.data?.capture_sessions || [];
  useEffect(() => {
    if (!projects.length) return;
    if (!projects.some((project) => project.id === viewer.projectId)) {
      viewer.setProjectId(projects[0].id);
      viewer.setExperimentId("");
      viewer.setRunId("");
      setActiveProjectId(projects[0].id);
    }
  }, [projects, viewer]);
  useEffect(() => {
    if (viewer.projectId) setActiveProjectId(viewer.projectId);
  }, [viewer.projectId]);
  useEffect(() => {
    if (
      viewer.experimentId &&
      !experiments.some((item) => item.id === viewer.experimentId)
    ) {
      viewer.setExperimentId("");
      viewer.setRunId("");
    }
  }, [experiments, viewer]);
  useEffect(() => {
    if (viewer.runId && !runs.some((item) => item.id === viewer.runId))
      viewer.setRunId("");
  }, [runs, viewer]);
  useEffect(() => {
    if (
      viewer.viewMode === "history" &&
      sessions.length &&
      !sessions.some((session) => session.id === history.captureSessionId)
    )
      history.setCaptureSessionId(sessions[0].id);
  }, [history, sessions, viewer.viewMode]);
  const selectedSession = sessions.find(
    (session: CaptureSession) => session.id === history.captureSessionId,
  );
  const projectName = projects.find(
    (project) => project.id === viewer.projectId,
  )?.name;
  const canAddPanel =
    viewer.viewMode === "live" &&
    !!viewer.projectId &&
    sensors.length > 0 &&
    !projectsQuery.isLoading &&
    !sensorsQuery.isLoading &&
    !sensorsQuery.error;
  const continueLive = () => {
    if (!history.effectiveSensorIds.length) return;
    const panelId = generateUUID();
    window.localStorage.setItem(
      `telemetry_panel_state_${panelId}`,
      JSON.stringify({
        title: "History -> Live",
        selectedSensorIds: history.effectiveSensorIds,
        valueMode: history.valueMode,
        maxPoints: 500,
        timeWindowSeconds: 300,
        useLatestAnchor: true,
        startFromTimestamp: history.lastTimestamp || undefined,
        startFromCursorBySensor: Object.keys(history.cursorBySensor).length
          ? history.cursorBySensor
          : undefined,
      }),
    );
    live.setPanelIds((value) => [...value, panelId]);
    viewer.setViewMode("live");
  };
  return (
    <div className="telemetry-view detail-page">
      {projectsQuery.isLoading && <Loading message="Загрузка проектов..." />}
      {!projectsQuery.isLoading && !projects.length && (
        <EmptyState message="У вас нет проектов. Создайте проект, чтобы просматривать телеметрию." />
      )}
      {projects.length > 0 && (
        <>
          <div
            className={`telemetry-view__workspace telemetry-view__workspace--${viewer.viewMode}`}
          >
            <TelemetryFilters
              projectId={viewer.projectId}
              experimentId={viewer.experimentId}
              runId={viewer.runId}
              viewMode={viewer.viewMode}
              projects={projects}
              experiments={experiments}
              runs={runs}
              projectsLoading={projectsQuery.isLoading}
              experimentsLoading={experimentsQuery.isLoading}
              runsLoading={runsQuery.isLoading}
              onProjectChange={(id) => {
                viewer.setProjectId(id);
                viewer.setExperimentId("");
                viewer.setRunId("");
                setActiveProjectId(id);
              }}
              onExperimentChange={(id) => {
                viewer.setExperimentId(id);
                viewer.setRunId("");
              }}
              onRunChange={viewer.setRunId}
              onViewModeChange={viewer.setViewMode}
            />
            {viewer.viewMode === "live" ? (
              <LiveTelemetryWorkspace
                panelIds={live.panelIds}
                sensors={sensors}
                sensorsLoading={sensorsQuery.isLoading}
                sensorsError={sensorsQuery.error}
                projectId={viewer.projectId}
                projectName={projectName}
                sessions={sessions}
                recentValues={live.recentValuesBySensor}
                panelSizes={live.panelSizes}
                containerWidth={live.panelsWrapWidth}
                containerRef={live.panelsWrapRef}
                draggingId={live.draggingPanelId}
                dragOverId={live.dragOverPanelId}
                onAddHistorySession={(id) => {
                  history.setCaptureSessionId(id);
                  viewer.setViewMode("history");
                }}
                onRemove={live.removePanel}
                onMove={live.movePanel}
                onSizeChange={live.onSizeChange}
                onRecordReceived={live.onRecordReceived}
                onDraggingChange={live.setDraggingPanelId}
                onDragOverChange={live.setDragOverPanelId}
              />
            ) : (
              <TelemetryHistoryPanel
                history={history}
                sessions={sessions}
                sensors={sensors}
                selectedSession={selectedSession}
                runId={viewer.runId}
                onContinueLive={continueLive}
                onExport={() => setShowExportModal(true)}
              />
            )}
          </div>
          {viewer.viewMode === "live" &&
            typeof document !== "undefined" &&
            createPortal(
              <FloatingActionButton
                onClick={live.addPanel}
                title="Добавить панель"
                ariaLabel="Добавить панель"
                disabled={!canAddPanel}
              />,
              document.body,
            )}
          {showExportModal && viewer.runId && (
            <TelemetryExportModal
              isOpen
              onClose={() => setShowExportModal(false)}
              runId={viewer.runId}
              mode="session"
              sessionId={history.captureSessionId || undefined}
              sessionOrdinal={selectedSession?.ordinal_number}
              sessions={sessions}
              sensors={sensors}
              initialCaptureSessionId={history.captureSessionId}
              initialSensorId={
                history.effectiveSensorIds.length === 1
                  ? history.effectiveSensorIds[0]
                  : ""
              }
              initialRawOrPhysical={history.valueMode}
              initialIncludeLate={history.includeLate}
              initialUseAggregation={history.useAggregated}
            />
          )}
        </>
      )}
    </div>
  );
}
async function listAllSensors(projectId: string) {
  const sensors: Sensor[] = [];
  let offset = 0;
  while (sensors.length < 5000) {
    const result = await sensorsApi.list({
      project_id: projectId,
      limit: 100,
      offset,
    });
    sensors.push(...result.sensors);
    if (
      result.sensors.length < 100 ||
      (result.total > 0 && sensors.length >= result.total)
    )
      break;
    offset += result.sensors.length;
  }
  return { sensors, total: sensors.length, page: 1, page_size: sensors.length };
}
