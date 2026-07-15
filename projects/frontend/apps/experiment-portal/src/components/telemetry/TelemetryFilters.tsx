import {
  FlaskIcon,
  FolderIcon,
  LiveSwitch,
  MaterialSelect,
  PlayCircleIcon,
} from "../common";
import type { TelemetryViewMode } from "../../hooks/useTelemetryViewerState";

interface Option {
  id: string;
  name: string;
}
interface TelemetryFiltersProps {
  projectId: string;
  experimentId: string;
  runId: string;
  viewMode: TelemetryViewMode;
  projects: Option[];
  experiments: Option[];
  runs: Option[];
  projectsLoading: boolean;
  experimentsLoading: boolean;
  runsLoading: boolean;
  onProjectChange: (id: string) => void;
  onExperimentChange: (id: string) => void;
  onRunChange: (id: string) => void;
  onViewModeChange: (mode: TelemetryViewMode) => void;
}

export default function TelemetryFilters({
  projectId,
  experimentId,
  runId,
  viewMode,
  projects,
  experiments,
  runs,
  projectsLoading,
  experimentsLoading,
  runsLoading,
  onProjectChange,
  onExperimentChange,
  onRunChange,
  onViewModeChange,
}: TelemetryFiltersProps) {
  return (
    <section className="telemetry-view__filters card">
      <div className="filter-capsule signal-route-capsule">
        <LiveSwitch
          live={viewMode === "live"}
          onChange={(live) => onViewModeChange(live ? "live" : "history")}
        />
        <MaterialSelect
          id="telemetry_project_id"
          label="Проект"
          placeholder="Выберите проект"
          value={projectId}
          onChange={onProjectChange}
          disabled={projectsLoading}
          variant="pill"
          icon={<FolderIcon />}
        >
          {projects.map((item) => (
            <option key={item.id} value={item.id}>
              {item.name}
            </option>
          ))}
        </MaterialSelect>
        <MaterialSelect
          id="telemetry_experiment_id"
          label="Эксперимент"
          placeholder="Выберите эксперимент"
          value={experimentId}
          onChange={onExperimentChange}
          disabled={!projectId || experimentsLoading || projectsLoading}
          variant="pill"
          icon={<FlaskIcon />}
        >
          {experiments.map((item) => (
            <option key={item.id} value={item.id}>
              {item.name}
            </option>
          ))}
        </MaterialSelect>
        <MaterialSelect
          id="telemetry_run_id"
          label="Пуск"
          placeholder="Выберите пуск"
          value={runId}
          onChange={onRunChange}
          disabled={
            !experimentId ||
            runsLoading ||
            experimentsLoading ||
            projectsLoading
          }
          variant="pill"
          icon={<PlayCircleIcon />}
        >
          {runs.map((item) => (
            <option key={item.id} value={item.id}>
              {item.name}
            </option>
          ))}
        </MaterialSelect>
      </div>
    </section>
  );
}
