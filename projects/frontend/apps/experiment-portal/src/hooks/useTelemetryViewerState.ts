import { useEffect, useState } from "react";

export type TelemetryViewMode = "live" | "history";

interface StoredTelemetryViewerState {
  projectId: string;
  experimentId: string;
  runId: string;
  viewMode: TelemetryViewMode;
}

export default function useTelemetryViewerState() {
  const [projectId, setProjectId] = useState("");
  const [experimentId, setExperimentId] = useState("");
  const [runId, setRunId] = useState("");
  const [viewMode, setViewMode] = useState<TelemetryViewMode>("live");
  const [loaded, setLoaded] = useState(false);

  useEffect(() => {
    try {
      const value = JSON.parse(
        window.localStorage.getItem("telemetry_viewer_state") || "{}",
      ) as Partial<StoredTelemetryViewerState>;
      if (typeof value.projectId === "string") setProjectId(value.projectId);
      if (typeof value.experimentId === "string")
        setExperimentId(value.experimentId);
      if (typeof value.runId === "string") setRunId(value.runId);
      if (value.viewMode === "live" || value.viewMode === "history")
        setViewMode(value.viewMode);
    } catch {
      // Ignore malformed persisted state.
    } finally {
      setLoaded(true);
    }
  }, []);

  useEffect(() => {
    if (!loaded) return;
    window.localStorage.setItem(
      "telemetry_viewer_state",
      JSON.stringify({ projectId, experimentId, runId, viewMode }),
    );
  }, [experimentId, loaded, projectId, runId, viewMode]);

  return {
    projectId,
    setProjectId,
    experimentId,
    setExperimentId,
    runId,
    setRunId,
    viewMode,
    setViewMode,
  };
}
