import { useEffect, useMemo, useState } from 'react'
import { createPortal } from 'react-dom'
import { useQuery } from '@tanstack/react-query'
import { experimentsApi, projectsApi, runsApi, sensorsApi } from '../api/client'
import {
    EmptyState,
    Error as ErrorComponent,
    FloatingActionButton,
    Loading,
    MaterialSelect,
    PageHeader,
} from '../components/common'
import TelemetryPanel from '../components/TelemetryPanel'
import { setActiveProjectId } from '../utils/activeProject'
import { generateUUID } from '../utils/uuid'
import './TelemetryViewer.css'

function TelemetryViewer() {
    const [projectId, setProjectId] = useState<string>('')
    const [experimentId, setExperimentId] = useState<string>('')
    const [runId, setRunId] = useState<string>('')
    const [panelIds, setPanelIds] = useState<string[]>([])
    const [filtersOpen, setFiltersOpen] = useState(true)

    const { data: projectsData, isLoading: projectsLoading } = useQuery({
        queryKey: ['projects'],
        queryFn: () => projectsApi.list(),
    })

    useEffect(() => {
        if (!projectId && projectsData?.projects?.length) {
            const id = projectsData.projects[0].id
            setProjectId(id)
            setActiveProjectId(id)
        }
    }, [projectId, projectsData])

    const { data: sensorsData, isLoading, error } = useQuery({
        queryKey: ['sensors', projectId],
        queryFn: () => sensorsApi.list({ project_id: projectId }),
        enabled: !!projectId,
    })

    const { data: experimentsData, isLoading: experimentsLoading, error: experimentsError } = useQuery({
        queryKey: ['experiments', projectId],
        queryFn: () => experimentsApi.list({ project_id: projectId, page_size: 100 }),
        enabled: !!projectId,
    })

    const { data: runsData, isLoading: runsLoading, error: runsError } = useQuery({
        queryKey: ['runs', experimentId],
        queryFn: () => runsApi.list(experimentId, { page_size: 100 }),
        enabled: !!experimentId,
    })

    const sensors = sensorsData?.sensors || []
    const experiments = experimentsData?.experiments || []
    const runs = runsData?.runs || []
    const hasProjects = !!projectsData?.projects?.length
    const canAddPanel =
        !!projectId &&
        sensors.length > 0 &&
        !projectsLoading &&
        !experimentsLoading &&
        !runsLoading &&
        !isLoading &&
        !error

    const projectName = projectsData?.projects.find((p) => p.id === projectId)?.name || 'Проект не выбран'
    const experimentName =
        experiments.find((experiment) => experiment.id === experimentId)?.name || 'Эксперимент не выбран'
    const runName = runs.find((run) => run.id === runId)?.name || 'Пуск не выбран'

    const addPanel = () => {
        setPanelIds((prev) => [...prev, generateUUID()])
    }

    const removePanel = (id: string) => {
        setPanelIds((prev) => prev.filter((panelId) => panelId !== id))
    }

    const panelTitleSeed = useMemo(() => {
        if (!projectId) return 'Панель'
        const projectName = projectsData?.projects.find((p) => p.id === projectId)?.name
        return projectName ? `Панель: ${projectName}` : 'Панель'
    }, [projectId, projectsData])

    return (
        <div className="telemetry-view">
            <PageHeader title="Телеметрия" />

            {projectsLoading && <Loading message="Загрузка проектов..." />}

            {!projectsLoading && !hasProjects && (
                <EmptyState message="У вас нет проектов. Создайте проект, чтобы просматривать телеметрию." />
            )}

            {hasProjects && (
                <div className={`telemetry-view__filters card${filtersOpen ? '' : ' telemetry-view__filters--collapsed'}`}>
                    <div className="telemetry-view__filters-header">
                        <div className="telemetry-view__summary">
                            {projectName} → {experimentName} → {runName}
                        </div>
                    </div>

                    {filtersOpen && (
                        <div className="telemetry-view__grid">
                            <MaterialSelect
                                id="telemetry_project_id"
                                label="Проект"
                                value={projectId}
                                onChange={(id) => {
                                    setProjectId(id)
                                    setActiveProjectId(id)
                                    setExperimentId('')
                                    setRunId('')
                                }}
                                disabled={projectsLoading}
                            >
                                <option value="">Выберите проект</option>
                                {projectsData?.projects.map((project) => (
                                    <option key={project.id} value={project.id}>
                                        {project.name}
                                    </option>
                                ))}
                            </MaterialSelect>

                            <MaterialSelect
                                id="telemetry_experiment_id"
                                label="Эксперимент"
                                value={experimentId}
                                onChange={(id) => {
                                    setExperimentId(id)
                                    setRunId('')
                                }}
                                disabled={!projectId || experimentsLoading || projectsLoading}
                            >
                                <option value="">Выберите эксперимент</option>
                                {experiments.map((experiment) => (
                                    <option key={experiment.id} value={experiment.id}>
                                        {experiment.name}
                                    </option>
                                ))}
                            </MaterialSelect>

                            <MaterialSelect
                                id="telemetry_run_id"
                                label="Пуск"
                                value={runId}
                                onChange={setRunId}
                                disabled={!experimentId || runsLoading || experimentsLoading || projectsLoading}
                            >
                                <option value="">Выберите пуск</option>
                                {runs.map((run) => (
                                    <option key={run.id} value={run.id}>
                                        {run.name}
                                    </option>
                                ))}
                            </MaterialSelect>
                        </div>
                    )}

                    {isLoading && <Loading message="Загрузка датчиков..." />}
                    {error && (
                        <ErrorComponent
                            message={
                                error instanceof Error
                                    ? error.message
                                    : 'Ошибка загрузки датчиков. Убедитесь, что выбран проект.'
                            }
                        />
                    )}
                    {!isLoading && !error && projectId && sensors.length === 0 && (
                        <EmptyState message="В выбранном проекте нет датчиков." />
                    )}
                    {experimentsLoading && <Loading message="Загрузка экспериментов..." />}
                    {!experimentsLoading && experimentsError && (
                        <ErrorComponent
                            message={
                                experimentsError instanceof Error
                                    ? experimentsError.message
                                    : 'Ошибка загрузки экспериментов.'
                            }
                        />
                    )}
                    {runsLoading && <Loading message="Загрузка запусков..." />}
                    {!runsLoading && runsError && (
                        <ErrorComponent
                            message={runsError instanceof Error ? runsError.message : 'Ошибка загрузки запусков.'}
                        />
                    )}

                    <div className="telemetry-view__collapse-wrap">
                        <button
                            type="button"
                            className="telemetry-view__collapse"
                            onClick={() => setFiltersOpen((prev) => !prev)}
                            aria-label={filtersOpen ? 'Свернуть фильтры' : 'Развернуть фильтры'}
                        >
                            {filtersOpen ? '︿' : '﹀'}
                        </button>
                    </div>
                </div>
            )}

            {canAddPanel && panelIds.length === 0 && (
                <EmptyState message="Добавьте панель, чтобы начать просмотр графиков." />
            )}

            <div className="telemetry-view__panels">
                {panelIds.map((panelId, index) => (
                    <TelemetryPanel
                        key={panelId}
                        panelId={panelId}
                        sensors={sensors}
                        title={`${panelTitleSeed} #${index + 1}`}
                        onRemove={() => removePanel(panelId)}
                    />
                ))}
            </div>

            {typeof document !== 'undefined' &&
                createPortal(
                    <FloatingActionButton
                        onClick={addPanel}
                        title="Добавить панель"
                        ariaLabel="Добавить панель"
                        disabled={!canAddPanel}
                    />,
                    document.body
                )}
        </div>
    )
}

export default TelemetryViewer
