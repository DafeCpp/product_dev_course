import { FlaskIcon, FolderIcon, LiveSwitch, MaterialSelect, PlayCircleIcon } from '../common'
import type { TelemetryViewMode } from '../../hooks/useTelemetryViewerState'

interface TelemetryFiltersProps {
    projectId: string; experimentId: string; runId: string; viewMode: TelemetryViewMode
    projects: Array<{ id: string; name: string }>; experiments: Array<{ id: string; name: string }>; runs: Array<{ id: string; name: string }>
    projectsLoading: boolean; experimentsLoading: boolean; runsLoading: boolean
    onProjectChange: (id: string) => void; onExperimentChange: (id: string) => void; onRunChange: (id: string) => void; onViewModeChange: (mode: TelemetryViewMode) => void
}

export default function TelemetryFilters({ projectId, experimentId, runId, viewMode, projects, experiments, runs, projectsLoading, experimentsLoading, runsLoading, onProjectChange, onExperimentChange, onRunChange, onViewModeChange }: TelemetryFiltersProps) {
    return <section className="telemetry-view__filters card"><div className="filter-capsule signal-route-capsule">
        <LiveSwitch live={viewMode === 'live'} onChange={(live) => onViewModeChange(live ? 'live' : 'history')} />
        <MaterialSelect id="telemetry_project_id" label="Проект" placeholder="Выберите проект" value={projectId} onChange={onProjectChange} disabled={projectsLoading} variant="pill" icon={<FolderIcon />}>{projects.map((project) => <option key={project.id} value={project.id}>{project.name}</option>)}</MaterialSelect>
        <MaterialSelect id="telemetry_experiment_id" label="Эксперимент" placeholder="Выберите эксперимент" value={experimentId} onChange={onExperimentChange} disabled={!projectId || experimentsLoading || projectsLoading} variant="pill" icon={<FlaskIcon />}>{experiments.map((experiment) => <option key={experiment.id} value={experiment.id}>{experiment.name}</option>)}</MaterialSelect>
        <MaterialSelect id="telemetry_run_id" label="Пуск" placeholder="Выберите пуск" value={runId} onChange={onRunChange} disabled={!experimentId || runsLoading || experimentsLoading || projectsLoading} variant="pill" icon={<PlayCircleIcon />}>{runs.map((run) => <option key={run.id} value={run.id}>{run.name}</option>)}</MaterialSelect>
    </div></section>
}
