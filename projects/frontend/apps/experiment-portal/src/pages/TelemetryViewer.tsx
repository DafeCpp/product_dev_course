import { useEffect, useMemo, useRef, useState } from 'react'
import { createPortal } from 'react-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import Plotly from 'plotly.js-dist-min'
import { captureSessionsApi, experimentsApi, projectsApi, runsApi, sensorsApi, telemetryApi } from '../api/client'
import { EmptyState, Error as ErrorComponent, FloatingActionButton, Loading, MaterialSelect } from '../components/common'
import TelemetryPanel from '../components/TelemetryPanel'
import { setActiveProjectId } from '../utils/activeProject'
import { generateUUID } from '../utils/uuid'
import { notifyError, notifySuccess } from '../utils/notify'
import type { CaptureSession, Sensor, TelemetryQueryRecord, TelemetryAggregatedRecord } from '../types'
import './TelemetryViewer.scss'

function hexToRgba(hex: string, alpha: number): string {
    const r = parseInt(hex.slice(1, 3), 16)
    const g = parseInt(hex.slice(3, 5), 16)
    const b = parseInt(hex.slice(5, 7), 16)
    return `rgba(${r},${g},${b},${alpha})`
}

type TelemetryViewerState = {
    projectId: string
    experimentId: string
    runId: string
    filtersOpen: boolean
    viewMode: TelemetryViewMode
}

type TelemetryViewMode = 'live' | 'history'
type HistoryValueMode = 'physical' | 'raw'
type TelemetryHistoryState = {
    captureSessionId: string
    sensorIds: string[]
    valueMode: HistoryValueMode
    includeLate: boolean
    maxPoints: number
    order: 'asc' | 'desc'
}

const HISTORY_MAX_POINTS_DEFAULT = 5000
const HISTORY_MAX_POINTS_LIMIT = 20000
const HISTORY_PAGE_SIZE = 2000
const HISTORY_MAX_SENSORS = 50

function TelemetryViewer() {
    const [projectId, setProjectId] = useState<string>('')
    const [experimentId, setExperimentId] = useState<string>('')
    const [runId, setRunId] = useState<string>('')
    const [panelIds, setPanelIds] = useState<string[]>([])
    const [filtersOpen, setFiltersOpen] = useState(true)
    const [viewMode, setViewMode] = useState<TelemetryViewMode>('live')
    const [draggingPanelId, setDraggingPanelId] = useState<string | null>(null)
    const [dragOverPanelId, setDragOverPanelId] = useState<string | null>(null)
    const panelsLoadedRef = useRef(false)
    const viewerStateLoadedRef = useRef(false)
    const historyStateLoadedRef = useRef(false)
    const [panelSizes, setPanelSizes] = useState<Record<string, { width: number; height: number }>>({})
    const panelsWrapRef = useRef<HTMLDivElement | null>(null)
    const [panelsWrapWidth, setPanelsWrapWidth] = useState(0)
    const historyPlotRef = useRef<HTMLDivElement | null>(null)

    const [historyCaptureSessionId, setHistoryCaptureSessionId] = useState('')
    const [historySensorIds, setHistorySensorIds] = useState<string[]>([])
    const [historyValueMode, setHistoryValueMode] = useState<HistoryValueMode>('physical')
    const [historyIncludeLate, setHistoryIncludeLate] = useState(true)
    const [historyMaxPoints, setHistoryMaxPoints] = useState(HISTORY_MAX_POINTS_DEFAULT)
    const [historyOrder, setHistoryOrder] = useState<'asc' | 'desc'>('asc')
    const [historyUseAggregated, setHistoryUseAggregated] = useState(false)
    const [historyLoading, setHistoryLoading] = useState(false)
    const [historyError, setHistoryError] = useState<string | null>(null)
    const [historyPoints, setHistoryPoints] = useState<TelemetryQueryRecord[]>([])
    const [historyAggBuckets, setHistoryAggBuckets] = useState<TelemetryAggregatedRecord[]>([])
    const [historyLoadedCount, setHistoryLoadedCount] = useState(0)
    const [historyWasTruncated, setHistoryWasTruncated] = useState(false)
    const [historySessionFilter, setHistorySessionFilter] = useState('')
    const [historySensorFilter, setHistorySensorFilter] = useState('')

    useEffect(() => {
        if (typeof window === 'undefined') return
        const raw = window.localStorage.getItem('telemetry_panel_ids')
        if (!raw) {
            panelsLoadedRef.current = true
            return
        }
        try {
            const parsed = JSON.parse(raw)
            if (Array.isArray(parsed)) {
                const next = parsed.filter((id) => typeof id === 'string' && id.trim().length > 0)
                if (next.length > 0) setPanelIds(next)
            }
        } catch {
            // ignore malformed local storage
        } finally {
            panelsLoadedRef.current = true
        }
    }, [])

    useEffect(() => {
        if (typeof window === 'undefined') return
        const raw = window.localStorage.getItem('telemetry_viewer_state')
        if (!raw) {
            viewerStateLoadedRef.current = true
            return
        }
        try {
            const parsed = JSON.parse(raw) as Partial<TelemetryViewerState>
            if (typeof parsed.projectId === 'string') setProjectId(parsed.projectId)
            if (typeof parsed.experimentId === 'string') setExperimentId(parsed.experimentId)
            if (typeof parsed.runId === 'string') setRunId(parsed.runId)
            if (typeof parsed.filtersOpen === 'boolean') setFiltersOpen(parsed.filtersOpen)
            if (parsed.viewMode === 'live' || parsed.viewMode === 'history') setViewMode(parsed.viewMode)
        } catch {
            // ignore malformed local storage
        } finally {
            viewerStateLoadedRef.current = true
        }
    }, [])

    useEffect(() => {
        if (typeof window === 'undefined' || !viewerStateLoadedRef.current) return
        const payload: TelemetryViewerState = {
            projectId,
            experimentId,
            runId,
            filtersOpen,
            viewMode,
        }
        window.localStorage.setItem('telemetry_viewer_state', JSON.stringify(payload))
    }, [projectId, experimentId, runId, filtersOpen, viewMode])

    useEffect(() => {
        if (typeof window === 'undefined') return
        const raw = window.localStorage.getItem('telemetry_history_state')
        if (!raw) {
            historyStateLoadedRef.current = true
            return
        }
        try {
            const parsed = JSON.parse(raw) as Partial<TelemetryHistoryState>
            if (typeof parsed.captureSessionId === 'string') setHistoryCaptureSessionId(parsed.captureSessionId)
            if (Array.isArray(parsed.sensorIds)) {
                const next = parsed.sensorIds.filter((id) => typeof id === 'string' && id.trim().length > 0)
                const limited = next.slice(0, HISTORY_MAX_SENSORS)
                setHistorySensorIds(limited)
                if (next.length > HISTORY_MAX_SENSORS) {
                    setHistoryError('Можно выбрать не более 50 сенсоров')
                }
            }
            if (parsed.valueMode === 'physical' || parsed.valueMode === 'raw') setHistoryValueMode(parsed.valueMode)
            if (typeof parsed.includeLate === 'boolean') setHistoryIncludeLate(parsed.includeLate)
            if (typeof parsed.maxPoints === 'number' && Number.isFinite(parsed.maxPoints)) {
                setHistoryMaxPoints(Math.max(100, Math.min(20000, Math.round(parsed.maxPoints))))
            }
            if (parsed.order === 'asc' || parsed.order === 'desc') {
                setHistoryOrder(parsed.order)
            }
        } catch {
            // ignore malformed local storage
        } finally {
            historyStateLoadedRef.current = true
        }
    }, [])

    useEffect(() => {
        if (typeof window === 'undefined' || !historyStateLoadedRef.current) return
        const payload: TelemetryHistoryState = {
            captureSessionId: historyCaptureSessionId,
            sensorIds: historySensorIds,
            valueMode: historyValueMode,
            includeLate: historyIncludeLate,
            maxPoints: historyMaxPoints,
            order: historyOrder,
        }
        window.localStorage.setItem('telemetry_history_state', JSON.stringify(payload))
    }, [historyCaptureSessionId, historySensorIds, historyValueMode, historyIncludeLate, historyMaxPoints, historyOrder])

    useEffect(() => {
        if (typeof window === 'undefined' || !panelsLoadedRef.current) return
        window.localStorage.setItem('telemetry_panel_ids', JSON.stringify(panelIds))
    }, [panelIds])

    useEffect(() => {
        const element = panelsWrapRef.current
        if (!element || typeof ResizeObserver === 'undefined') return

        const updateSize = () => {
            const rect = element.getBoundingClientRect()
            setPanelsWrapWidth(Math.max(0, Math.round(rect.width)))
        }

        updateSize()
        const observer = new ResizeObserver(updateSize)
        observer.observe(element)

        return () => observer.disconnect()
    }, [])

    const handlePanelSizeChange = (panelId: string, size: { width: number; height: number }) => {
        setPanelSizes((prev) => {
            const existing = prev[panelId]
            if (existing && existing.width === size.width && existing.height === size.height) return prev
            return { ...prev, [panelId]: size }
        })
    }

    const { data: projectsData, isLoading: projectsLoading } = useQuery({
        queryKey: ['projects'],
        queryFn: () => projectsApi.list(),
    })

    useEffect(() => {
        if (!projectsData?.projects?.length) return
        const exists = projectsData.projects.some((project) => project.id === projectId)
        if (!projectId || !exists) {
            const id = projectsData.projects[0].id
            setProjectId(id)
            setExperimentId('')
            setRunId('')
            setActiveProjectId(id)
        }
    }, [projectId, projectsData])

    useEffect(() => {
        if (projectId) setActiveProjectId(projectId)
    }, [projectId])

    const { data: sensorsData, isLoading, error } = useQuery({
        queryKey: ['sensors', projectId],
        queryFn: async () => {
            // The backend paginates with limit/offset and caps limit at 100.
            // For Telemetry we want a complete sensor list for selection.
            const limit = 100
            let offset = 0
            let total = 0
            const collected: Sensor[] = []

            // Safety guard: avoid an infinite loop if upstream misbehaves.
            const maxTotalToFetch = 5000

            while (collected.length < maxTotalToFetch) {
                const resp = await sensorsApi.list({ project_id: projectId, limit, offset })
                total = resp.total
                collected.push(...resp.sensors)

                if (resp.sensors.length === 0) break
                offset += resp.sensors.length
                if (resp.sensors.length < limit) break
                if (total > 0 && collected.length >= total) break
            }

            return {
                sensors: collected,
                total,
                page: 1,
                page_size: collected.length,
            }
        },
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

    const { data: runDetail, isLoading: runDetailLoading } = useQuery({
        queryKey: ['run', runId],
        queryFn: () => runsApi.get(runId),
        enabled: !!runId,
    })

    const { data: runExperiment } = useQuery({
        queryKey: ['experiment', runDetail?.experiment_id],
        queryFn: () => experimentsApi.get(runDetail!.experiment_id),
        enabled: !!runDetail?.experiment_id,
    })

    const { data: captureSessionsData, isLoading: captureSessionsLoading, error: captureSessionsError } = useQuery({
        queryKey: ['capture-sessions', runId],
        queryFn: () => captureSessionsApi.list(runId, { page_size: 200 }),
        enabled: !!runId,
    })

    useEffect(() => {
        if (!experimentId) return
        const exists = experimentsData?.experiments?.some((experiment) => experiment.id === experimentId)
        if (!exists) {
            setExperimentId('')
            setRunId('')
        }
    }, [experimentId, experimentsData])

    useEffect(() => {
        if (!runId) return
        const exists = runsData?.runs?.some((run) => run.id === runId)
        if (!exists) setRunId('')
    }, [runId, runsData])

    useEffect(() => {
        if (viewMode !== 'history') return
        const sessions = captureSessionsData?.capture_sessions || []
        const exists = sessions.some((s) => s.id === historyCaptureSessionId)
        if (!historyCaptureSessionId || !exists) {
            setHistoryCaptureSessionId(sessions[0]?.id || '')
        }
    }, [captureSessionsData, historyCaptureSessionId, viewMode])

    const sensors = sensorsData?.sensors || []
    const experiments = experimentsData?.experiments || []
    const runs = runsData?.runs || []
    const captureSessions = captureSessionsData?.capture_sessions || []
    const activeCaptureSession = captureSessions.find(
        (s: CaptureSession) => s.status === 'running' || s.status === 'backfilling'
    )
    const canManageCaptureSession =
        !!runId &&
        !!runDetail &&
        !!runExperiment &&
        (runDetail.status === 'draft' || runDetail.status === 'running')

    const queryClient = useQueryClient()
    const createSessionMutation = useMutation({
        mutationFn: (notes?: string) => {
            if (!runExperiment) throw new Error('Experiment not loaded')
            const nextOrdinal =
                captureSessions.length > 0
                    ? Math.max(...captureSessions.map((s: CaptureSession) => s.ordinal_number)) + 1
                    : 1
            return captureSessionsApi.create(runId, {
                project_id: runExperiment.project_id,
                run_id: runId,
                ordinal_number: nextOrdinal,
                notes: notes || undefined,
            }, { project_id: runExperiment.project_id })
        },
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['capture-sessions', runId] })
            notifySuccess('Отсчёт запущен')
        },
        onError: (err: unknown) => {
            const msg =
                (err as any)?.response?.data?.message ||
                (err as any)?.response?.data?.error ||
                (err as Error)?.message ||
                'Не удалось запустить отсчёт'
            notifyError(msg)
        },
    })

    const stopSessionMutation = useMutation({
        mutationFn: (sessionId: string) => captureSessionsApi.stop(runId, sessionId),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['capture-sessions', runId] })
            notifySuccess('Отсчёт остановлен')
        },
        onError: (err: unknown) => {
            const msg =
                (err as any)?.response?.data?.message ||
                (err as any)?.response?.data?.error ||
                (err as Error)?.message ||
                'Не удалось остановить отсчёт'
            notifyError(msg)
        },
    })

    const startBackfillMutation = useMutation({
        mutationFn: (sessionId: string) => captureSessionsApi.startBackfill(runId, sessionId),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['capture-sessions', runId] })
            notifySuccess('Догрузка запущена — сессия в режиме backfilling')
        },
        onError: (err: unknown) => {
            const msg =
                (err as any)?.response?.data?.message ||
                (err as any)?.response?.data?.error ||
                (err as Error)?.message ||
                'Не удалось запустить догрузку'
            notifyError(msg)
        },
    })

    const completeBackfillMutation = useMutation({
        mutationFn: (sessionId: string) => captureSessionsApi.completeBackfill(runId, sessionId),
        onSuccess: (data) => {
            queryClient.invalidateQueries({ queryKey: ['capture-sessions', runId] })
            const attached = (data as any)?.attached_records ?? 0
            notifySuccess(`Догрузка завершена — привязано записей: ${attached}`)
        },
        onError: (err: unknown) => {
            const msg =
                (err as any)?.response?.data?.message ||
                (err as any)?.response?.data?.error ||
                (err as Error)?.message ||
                'Не удалось завершить догрузку'
            notifyError(msg)
        },
    })

    const selectedHistorySession = captureSessions.find(
        (s: CaptureSession) => s.id === historyCaptureSessionId
    )
    const canStartBackfill =
        viewMode === 'history' &&
        !!selectedHistorySession &&
        selectedHistorySession.status === 'succeeded'
    const canCompleteBackfill =
        viewMode === 'history' &&
        !!selectedHistorySession &&
        selectedHistorySession.status === 'backfilling'

    const hasProjects = !!projectsData?.projects?.length
    const isLiveMode = viewMode === 'live'
    const canAddPanel =
        isLiveMode &&
        !!projectId &&
        sensors.length > 0 &&
        !projectsLoading &&
        !experimentsLoading &&
        !runsLoading &&
        !isLoading &&
        !error

    const addPanel = () => {
        setPanelIds((prev) => [...prev, generateUUID()])
    }

    const removePanel = (id: string) => {
        setPanelIds((prev) => prev.filter((panelId) => panelId !== id))
    }

    const movePanel = (sourceId: string, targetId: string) => {
        if (sourceId === targetId) return
        setPanelIds((prev) => {
            const next = [...prev]
            const fromIndex = next.indexOf(sourceId)
            const toIndex = next.indexOf(targetId)
            if (fromIndex === -1 || toIndex === -1) return prev
            next.splice(fromIndex, 1)
            next.splice(toIndex, 0, sourceId)
            return next
        })
    }

    const panelTitleSeed = useMemo(() => {
        if (!projectId) return 'Панель'
        const projectName = projectsData?.projects.find((p) => p.id === projectId)?.name
        return projectName ? `Панель: ${projectName}` : 'Панель'
    }, [projectId, projectsData])

    const availableHistorySensors = useMemo(
        () => sensors.filter((s) => !historySensorIds.includes(s.id)),
        [sensors, historySensorIds]
    )

    const filteredHistorySensors = useMemo(() => {
        const query = historySensorFilter.trim().toLowerCase()
        if (!query) return availableHistorySensors
        return availableHistorySensors.filter((sensor) => {
            const hay = `${sensor.name} ${sensor.type} ${sensor.id}`.toLowerCase()
            return hay.includes(query)
        })
    }, [availableHistorySensors, historySensorFilter])

    const historySensorOverLimit = historySensorIds.length > HISTORY_MAX_SENSORS
    const historySensorLimitReached = historySensorIds.length >= HISTORY_MAX_SENSORS

    useEffect(() => {
        if (historySensorOverLimit && historyError?.includes('сенсоров')) return
        if (!historySensorOverLimit && historyError === 'Можно выбрать не более 50 сенсоров') {
            setHistoryError(null)
        }
    }, [historySensorOverLimit, historyError])

    useEffect(() => {
        if (historyMaxPoints > HISTORY_MAX_POINTS_LIMIT) {
            setHistoryMaxPoints(HISTORY_MAX_POINTS_LIMIT)
        }
    }, [historyMaxPoints])

    useEffect(() => {
        setHistoryPoints([])
        setHistoryLoadedCount(0)
        setHistoryWasTruncated(false)
        setHistoryError(null)
    }, [historyCaptureSessionId, historySensorIds, historyIncludeLate, historyOrder, historyMaxPoints])

    const historySensorsById = useMemo(() => {
        const map = new Map<string, Sensor>()
        sensors.forEach((sensor) => map.set(sensor.id, sensor))
        return map
    }, [sensors])

    const historySeriesData = useMemo(() => {
        const bySensor = new Map<string, TelemetryQueryRecord[]>()
        historyPoints.forEach((point) => {
            const list = bySensor.get(point.sensor_id) || []
            list.push(point)
            bySensor.set(point.sensor_id, list)
        })
        return Array.from(bySensor.entries()).map(([sensorId, points]) => {
            const sensor = historySensorsById.get(sensorId)
            const ordered = points.slice().sort((a, b) => a.id - b.id)
            return {
                id: sensorId,
                name: sensor?.name || sensorId,
                x: ordered.map((p) => p.timestamp),
                y: ordered.map((p) =>
                    historyValueMode === 'physical' ? p.physical_value : p.raw_value
                ),
            }
        })
    }, [historyPoints, historySensorsById, historyValueMode])

    // Aggregated series: group by sensor_id+signal, build avg / min / max arrays
    const historyAggSeriesData = useMemo(() => {
        type AggGroup = {
            sensorId: string
            signal: string | null
            buckets: TelemetryAggregatedRecord[]
        }
        const key = (b: TelemetryAggregatedRecord) =>
            `${b.sensor_id ?? ''}::${b.signal ?? ''}`
        const groups = new Map<string, AggGroup>()
        historyAggBuckets.forEach((b) => {
            const k = key(b)
            let g = groups.get(k)
            if (!g) {
                g = { sensorId: b.sensor_id ?? '', signal: b.signal, buckets: [] }
                groups.set(k, g)
            }
            g.buckets.push(b)
        })
        return Array.from(groups.values()).map((g) => {
            const ordered = g.buckets.slice().sort(
                (a, b) => new Date(a.bucket).getTime() - new Date(b.bucket).getTime()
            )
            const sensor = historySensorsById.get(g.sensorId)
            const label = `${sensor?.name || g.sensorId}${g.signal ? ` / ${g.signal}` : ''}`
            return {
                id: g.sensorId,
                signal: g.signal,
                name: label,
                x: ordered.map((r) => r.bucket),
                avg: ordered.map((r) =>
                    historyValueMode === 'physical' ? r.avg_physical : r.avg_raw
                ),
                min: ordered.map((r) =>
                    historyValueMode === 'physical' ? r.min_physical : r.min_raw
                ),
                max: ordered.map((r) =>
                    historyValueMode === 'physical' ? r.max_physical : r.max_raw
                ),
                count: ordered.map((r) => r.sample_count),
            }
        })
    }, [historyAggBuckets, historySensorsById, historyValueMode])

    const historyEffectiveSensorIds = useMemo(() => {
        if (historySensorIds.length > 0) return historySensorIds
        const unique = new Set<string>()
        historyPoints.forEach((point) => unique.add(point.sensor_id))
        return Array.from(unique)
    }, [historySensorIds, historyPoints])

    const historyLastTimestamp = useMemo(() => {
        let latest = 0
        historyPoints.forEach((point) => {
            const parsed = Date.parse(point.timestamp)
            if (Number.isFinite(parsed) && parsed > latest) latest = parsed
        })
        return latest > 0 ? new Date(latest).toISOString() : null
    }, [historyPoints])

    const historyCursorBySensor = useMemo(() => {
        const latestBySensor = new Map<string, { tsMs: number; id: number }>()
        historyPoints.forEach((point) => {
            const tsMs = Date.parse(point.timestamp)
            if (!Number.isFinite(tsMs)) return
            const current = latestBySensor.get(point.sensor_id)
            if (!current || tsMs > current.tsMs || (tsMs === current.tsMs && point.id > current.id)) {
                latestBySensor.set(point.sensor_id, { tsMs, id: point.id })
            }
        })
        const result: Record<string, { timestamp: string; id: number }> = {}
        latestBySensor.forEach((value, sensorId) => {
            result[sensorId] = { timestamp: new Date(value.tsMs).toISOString(), id: value.id }
        })
        return result
    }, [historyPoints])

    const historyDisplayMaxPoints = useMemo(() => {
        const value =
            Number.isFinite(historyMaxPoints) && historyMaxPoints > 0 ? historyMaxPoints : HISTORY_MAX_POINTS_DEFAULT
        return Math.min(HISTORY_MAX_POINTS_LIMIT, value)
    }, [historyMaxPoints])

    const historySessionOptions = useMemo(() => {
        const query = historySessionFilter.trim().toLowerCase()
        const formatTimestamp = (value?: string | null) => {
            if (!value) return '—'
            const date = new Date(value)
            return Number.isNaN(date.getTime()) ? value : date.toLocaleString()
        }
        const matchesQuery = (session: CaptureSession) => {
            if (!query) return true
            const parts = [
                session.id,
                session.ordinal_number,
                session.status,
                session.notes,
                session.started_at,
                session.stopped_at,
            ]
                .filter(Boolean)
                .join(' ')
                .toLowerCase()
            return parts.includes(query)
        }
        return captureSessions
            .filter(matchesQuery)
            .map((session) => ({
                id: session.id,
                label: `#${session.ordinal_number} · ${session.status} · ${formatTimestamp(session.started_at)} → ${formatTimestamp(
                    session.stopped_at
                )}`,
            }))
    }, [captureSessions, historySessionFilter])

    const historyHasData = useMemo(
        () =>
            historyUseAggregated
                ? historyAggSeriesData.some((s) => s.avg.length > 0)
                : historySeriesData.some((series) => series.y.length > 0),
        [historySeriesData, historyAggSeriesData, historyUseAggregated]
    )

    const SERIES_COLORS = ['#2563eb', '#16a34a', '#f97316', '#a855f7', '#06b6d4', '#e11d48']

    const historyPlotlyData = useMemo(() => {
        if (historyUseAggregated) {
            // For each sensor+signal group: min band (invisible base), max band (filled to min), avg line
            const traces: any[] = []
            historyAggSeriesData.forEach((series, index) => {
                const color = SERIES_COLORS[index % SERIES_COLORS.length]
                // min line — invisible lower bound for fill
                traces.push({
                    x: series.x,
                    y: series.min,
                    type: 'scatter' as const,
                    mode: 'lines' as const,
                    name: `${series.name} min`,
                    line: { color: 'transparent', width: 0 },
                    showlegend: false,
                    hoverinfo: 'skip' as const,
                })
                // max line — filled down to min
                traces.push({
                    x: series.x,
                    y: series.max,
                    type: 'scatter' as const,
                    mode: 'lines' as const,
                    name: `${series.name} range`,
                    line: { color: 'transparent', width: 0 },
                    fill: 'tonexty' as const,
                    fillcolor: hexToRgba(color, 0.15),
                    showlegend: false,
                    hovertemplate: `min: %{y:.3f}<extra>${series.name} max</extra>`,
                })
                // avg line
                traces.push({
                    x: series.x,
                    y: series.avg,
                    type: 'scatter' as const,
                    mode: 'lines' as const,
                    name: `${series.name} avg`,
                    line: { color, width: 2 },
                    hovertemplate: `avg: %{y:.3f}<extra>${series.name}</extra>`,
                })
            })
            return traces
        }
        return historySeriesData.map((series, index) => ({
            x: series.x,
            y: series.y,
            type: 'scattergl' as const,
            mode: 'lines' as const,
            name: series.name,
            line: {
                color: SERIES_COLORS[index % SERIES_COLORS.length],
                width: 2,
            },
            hovertemplate: '%{x}<br>%{y:.3f}<extra></extra>',
        }))
    }, [historySeriesData, historyAggSeriesData, historyUseAggregated])

    const historyPlotlyLayout = useMemo(
        () => ({
            autosize: true,
            margin: { l: 42, r: 14, t: 12, b: 24 },
            showlegend: true,
            paper_bgcolor: 'rgba(0,0,0,0)',
            plot_bgcolor: 'rgba(15, 23, 42, 0.04)',
            xaxis: {
                showgrid: true,
                zeroline: false,
                gridcolor: 'rgba(15, 23, 42, 0.12)',
                tickfont: { size: 10, color: '#475569' },
                ticks: 'outside' as const,
                tickcolor: 'rgba(15, 23, 42, 0.12)',
            },
            yaxis: {
                showgrid: true,
                zeroline: false,
                gridcolor: 'rgba(15, 23, 42, 0.12)',
                tickfont: { size: 10, color: '#475569' },
                ticks: 'outside' as const,
                tickcolor: 'rgba(15, 23, 42, 0.12)',
            },
        }),
        []
    )

    const historyPlotlyConfig = useMemo(
        () => ({
            responsive: true,
            displayModeBar: false,
            displaylogo: false,
        }),
        []
    )

    useEffect(() => {
        const element = historyPlotRef.current
        if (!element) return
        const data = historyHasData ? historyPlotlyData : []
        Plotly.react(element, data, historyPlotlyLayout, historyPlotlyConfig)
    }, [historyPlotlyData, historyPlotlyLayout, historyPlotlyConfig, historyHasData])

    useEffect(() => {
        const element = historyPlotRef.current
        if (!element || typeof ResizeObserver === 'undefined') return
        const observer = new ResizeObserver(() => {
            Plotly.Plots.resize(element)
        })
        observer.observe(element)
        return () => observer.disconnect()
    }, [])

    useEffect(() => {
        return () => {
            const element = historyPlotRef.current
            if (element) Plotly.purge(element)
        }
    }, [])

    const addHistorySensor = (sensorId: string) => {
        if (!sensorId) return
        setHistorySensorIds((prev) => {
            if (prev.includes(sensorId)) return prev
            if (prev.length >= HISTORY_MAX_SENSORS) {
                setHistoryError('Можно выбрать не более 50 сенсоров')
                return prev
            }
            return [...prev, sensorId]
        })
    }

    const addAllHistorySensors = () => {
        if (availableHistorySensors.length === 0) return
        setHistorySensorIds((prev) => {
            const next = new Set(prev)
            for (const sensor of availableHistorySensors) {
                if (next.size >= HISTORY_MAX_SENSORS) break
                next.add(sensor.id)
            }
            if (next.size >= HISTORY_MAX_SENSORS && availableHistorySensors.length > next.size) {
                setHistoryError('Можно выбрать не более 50 сенсоров')
            }
            return Array.from(next)
        })
    }

    const removeHistorySensor = (sensorId: string) => {
        setHistorySensorIds((prev) => prev.filter((id) => id !== sensorId))
    }

    const clearHistorySensors = () => {
        setHistorySensorIds([])
    }

    const continueHistoryInLive = () => {
        if (typeof window === 'undefined') return
        const sensorIds = historyEffectiveSensorIds
        if (sensorIds.length === 0) return
        const panelId = generateUUID()
        const startFromCursorBySensor =
            Object.keys(historyCursorBySensor).length > 0 ? historyCursorBySensor : undefined
        const payload = {
            title: 'History -> Live',
            selectedSensorIds: sensorIds,
            valueMode: historyValueMode,
            maxPoints: 500,
            timeWindowSeconds: 300,
            useLatestAnchor: true,
            startFromTimestamp: historyLastTimestamp ?? undefined,
            startFromCursorBySensor,
        }
        window.localStorage.setItem(`telemetry_panel_state_${panelId}`, JSON.stringify(payload))
        setPanelIds((prev) => [...prev, panelId])
        setViewMode('live')
    }

    const loadHistory = async () => {
        if (!historyCaptureSessionId) return
        setHistoryError(null)
        if (historySensorOverLimit) {
            setHistoryError('Можно выбрать не более 50 сенсоров')
            return
        }
        setHistoryLoading(true)
        setHistoryPoints([])
        setHistoryAggBuckets([])
        setHistoryLoadedCount(0)
        setHistoryWasTruncated(false)
        try {
            if (historyUseAggregated) {
                // --- Aggregated (1m buckets from continuous aggregate) ---
                const safeLimit = Math.min(
                    settings.telemetry_query_max_limit ?? HISTORY_MAX_POINTS_LIMIT,
                    Number.isFinite(historyMaxPoints) && historyMaxPoints > 0 ? historyMaxPoints : HISTORY_MAX_POINTS_DEFAULT,
                )
                const resp = await telemetryApi.aggregated({
                    capture_session_id: historyCaptureSessionId,
                    sensor_id: historySensorIds.length > 0 ? historySensorIds : undefined,
                    limit: safeLimit,
                    order: historyOrder,
                })
                setHistoryAggBuckets(resp.buckets)
                setHistoryLoadedCount(resp.buckets.length)
            } else {
                // --- Raw points ---
                let sinceId = 0
                const collected: TelemetryQueryRecord[] = []
                const safeMaxPointsRaw =
                    Number.isFinite(historyMaxPoints) && historyMaxPoints > 0 ? historyMaxPoints : HISTORY_MAX_POINTS_DEFAULT
                const safeMaxPoints = Math.min(HISTORY_MAX_POINTS_LIMIT, safeMaxPointsRaw)
                let lastHasMore = false
                while (collected.length < safeMaxPoints) {
                    const pageLimit = Math.min(HISTORY_PAGE_SIZE, safeMaxPoints - collected.length)
                    const resp = await telemetryApi.query({
                        capture_session_id: historyCaptureSessionId,
                        sensor_id: historySensorIds.length > 0 ? historySensorIds : undefined,
                        since_id: sinceId,
                        limit: pageLimit,
                        include_late: historyIncludeLate,
                        order: historyOrder,
                    })
                    collected.push(...resp.points)
                    setHistoryLoadedCount(collected.length)
                    lastHasMore = !!resp.next_since_id && resp.points.length > 0
                    if (!lastHasMore) break
                    sinceId = resp.next_since_id ?? sinceId
                }
                setHistoryPoints(collected)
                setHistoryWasTruncated(lastHasMore && collected.length >= safeMaxPoints)
            }
        } catch (err: any) {
            setHistoryError(err?.message || 'Ошибка загрузки истории')
        } finally {
            setHistoryLoading(false)
        }
    }

    // Placeholder for settings object used above (avoids introducing a new dependency)
    const settings = { telemetry_query_max_limit: HISTORY_MAX_POINTS_LIMIT }

    return (
        <div className="telemetry-view">
            {projectsLoading && <Loading message="Загрузка проектов..." />}

            {!projectsLoading && !hasProjects && (
                <EmptyState message="У вас нет проектов. Создайте проект, чтобы просматривать телеметрию." />
            )}

            {hasProjects && (
                <div className={`telemetry-view__filters card${filtersOpen ? '' : ' telemetry-view__filters--collapsed'}`}>
                    <div
                        className={`telemetry-view__filters-body${filtersOpen ? ' telemetry-view__filters-body--open' : ''
                            }`}
                        aria-hidden={!filtersOpen}
                    >
                        <div className="telemetry-view__grid">
                            <div className="form-group telemetry-view__mode">
                                <label>Режим</label>
                                <div className="telemetry-view__mode-toggle">
                                    <label>
                                        <input
                                            type="radio"
                                            name="telemetry-view-mode"
                                            checked={viewMode === 'live'}
                                            onChange={() => setViewMode('live')}
                                        />
                                        live
                                    </label>
                                    <label>
                                        <input
                                            type="radio"
                                            name="telemetry-view-mode"
                                            checked={viewMode === 'history'}
                                            onChange={() => setViewMode('history')}
                                        />
                                        history
                                    </label>
                                </div>
                            </div>
                            <MaterialSelect
                                id="telemetry_project_id"
                                label="Проект"
                                placeholder="Выберите проект"
                                value={projectId}
                                onChange={(id) => {
                                    setProjectId(id)
                                    setActiveProjectId(id)
                                    setExperimentId('')
                                    setRunId('')
                                }}
                                disabled={projectsLoading}
                            >
                                {projectsData?.projects.map((project) => (
                                    <option key={project.id} value={project.id}>
                                        {project.name}
                                    </option>
                                ))}
                            </MaterialSelect>

                            <MaterialSelect
                                id="telemetry_experiment_id"
                                label="Эксперимент"
                                placeholder="Выберите эксперимент"
                                value={experimentId}
                                onChange={(id) => {
                                    setExperimentId(id)
                                    setRunId('')
                                }}
                                disabled={!projectId || experimentsLoading || projectsLoading}
                            >
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
                                placeholder="Выберите пуск"
                                disabled={!experimentId || runsLoading || experimentsLoading || projectsLoading}
                            >
                                {runs.map((run) => (
                                    <option key={run.id} value={run.id}>
                                        {run.name}
                                    </option>
                                ))}
                            </MaterialSelect>

                            {canManageCaptureSession && (
                                <div className="telemetry-view__capture-actions form-group">
                                    <label className="telemetry-view__capture-actions-label">Отсчёт (capture session)</label>
                                    <div className="telemetry-view__capture-actions-btns">
                                        {activeCaptureSession ? (
                                            <button
                                                type="button"
                                                className="btn btn-danger btn-sm"
                                                onClick={() => {
                                                    if (window.confirm('Остановить отсчёт?')) {
                                                        stopSessionMutation.mutate(activeCaptureSession.id)
                                                    }
                                                }}
                                                disabled={stopSessionMutation.isPending}
                                            >
                                                {stopSessionMutation.isPending ? 'Остановка...' : 'Остановить отсчёт'}
                                            </button>
                                        ) : (
                                            <button
                                                type="button"
                                                className="btn btn-primary btn-sm"
                                                onClick={() => {
                                                    const notes = window.prompt('Заметки (опционально):')
                                                    if (notes === null) return
                                                    createSessionMutation.mutate(notes.trim() || undefined)
                                                }}
                                                disabled={
                                                    createSessionMutation.isPending ||
                                                    runDetailLoading ||
                                                    !runExperiment
                                                }
                                            >
                                                {createSessionMutation.isPending
                                                    ? 'Создание...'
                                                    : 'Старт отсчёта'}
                                            </button>
                                        )}
                                    </div>
                                </div>
                            )}

                            {viewMode === 'history' && (
                                <div className="form-group">
                                    <label htmlFor="telemetry_capture_session_filter">Фильтр сессий</label>
                                    <input
                                        id="telemetry_capture_session_filter"
                                        type="text"
                                        className="telemetry-view__text-input"
                                        value={historySessionFilter}
                                        onChange={(event) => setHistorySessionFilter(event.target.value)}
                                        placeholder="Номер, статус, заметка, дата"
                                    />
                                </div>
                            )}

                            {viewMode === 'history' && (
                                <MaterialSelect
                                    id="telemetry_capture_session_id"
                                    label="Capture session"
                                    value={historyCaptureSessionId}
                                    onChange={setHistoryCaptureSessionId}
                                    placeholder="Выберите сессию"
                                    disabled={!runId || captureSessionsLoading}
                                >
                                    {historySessionOptions.length === 0 && (
                                        <option value="" disabled>
                                            {historySessionFilter.trim()
                                                ? 'Ничего не найдено'
                                                : 'Нет доступных сессий'}
                                        </option>
                                    )}
                                    {historySessionOptions.map((session) => (
                                        <option key={session.id} value={session.id}>
                                            {session.label}
                                        </option>
                                    ))}
                                </MaterialSelect>
                            )}

                            {(canStartBackfill || canCompleteBackfill) && (
                                <div className="telemetry-view__backfill-actions form-group">
                                    <label>Догрузка данных (backfill)</label>
                                    <div className="telemetry-view__backfill-btns">
                                        {canStartBackfill && (
                                            <button
                                                type="button"
                                                className="btn btn-secondary btn-sm"
                                                disabled={startBackfillMutation.isPending}
                                                onClick={() => {
                                                    if (window.confirm(
                                                        'Перевести сессию в режим догрузки (backfilling)?\n\n' +
                                                        'Новые данные от датчиков будут привязаны к этой сессии.'
                                                    )) {
                                                        startBackfillMutation.mutate(historyCaptureSessionId)
                                                    }
                                                }}
                                            >
                                                {startBackfillMutation.isPending
                                                    ? 'Запуск...'
                                                    : 'Начать догрузку'}
                                            </button>
                                        )}
                                        {canCompleteBackfill && (
                                            <button
                                                type="button"
                                                className="btn btn-primary btn-sm"
                                                disabled={completeBackfillMutation.isPending}
                                                onClick={() => {
                                                    if (window.confirm(
                                                        'Завершить догрузку?\n\n' +
                                                        'Все late-записи будут привязаны к сессии, статус вернётся в succeeded.'
                                                    )) {
                                                        completeBackfillMutation.mutate(historyCaptureSessionId)
                                                    }
                                                }}
                                            >
                                                {completeBackfillMutation.isPending
                                                    ? 'Завершение...'
                                                    : 'Завершить догрузку'}
                                            </button>
                                        )}
                                    </div>
                                </div>
                            )}
                        </div>

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
                        {viewMode === 'history' && captureSessionsLoading && <Loading message="Загрузка сессий..." />}
                        {viewMode === 'history' && captureSessionsError && (
                            <ErrorComponent
                                message={
                                    captureSessionsError instanceof Error
                                        ? captureSessionsError.message
                                        : 'Ошибка загрузки сессий.'
                                }
                            />
                        )}
                        {viewMode === 'history' &&
                            !captureSessionsLoading &&
                            !captureSessionsError &&
                            runId &&
                            captureSessions.length === 0 && (
                                <EmptyState message="В выбранном запуске нет capture sessions." />
                            )}
                    </div>


                    <button
                        type="button"
                        className="telemetry-view__collapse"
                        onClick={() => setFiltersOpen((prev) => !prev)}
                        aria-label={filtersOpen ? 'Свернуть фильтры' : 'Развернуть фильтры'}
                    >
                        {filtersOpen ? '︿' : '﹀'}
                    </button>

                </div>
            )}

            {canAddPanel && panelIds.length === 0 && (
                <EmptyState message="Добавьте панель, чтобы начать просмотр графиков." />
            )}

            {isLiveMode ? (
                <div className="telemetry-view__panels" ref={panelsWrapRef}>
                    {panelIds.map((panelId, index) => {
                        const panelSize = panelSizes[panelId]
                        const isWide = panelsWrapWidth > 0 && panelSize ? panelSize.width > panelsWrapWidth / 2 : false
                        return (
                            <div
                                key={panelId}
                                className={`telemetry-view__panel-item${draggingPanelId === panelId ? ' telemetry-view__panel-item--dragging' : ''
                                    }${dragOverPanelId === panelId ? ' telemetry-view__panel-item--over' : ''}${isWide ? ' telemetry-view__panel-item--full' : ''
                                    }`}
                                onDragOver={(event) => {
                                    if (!draggingPanelId || draggingPanelId === panelId) return
                                    event.preventDefault()
                                    event.dataTransfer.dropEffect = 'move'
                                    setDragOverPanelId(panelId)
                                }}
                                onDragLeave={(event) => {
                                    if (event.currentTarget.contains(event.relatedTarget as Node)) return
                                    setDragOverPanelId((prev) => (prev === panelId ? null : prev))
                                }}
                                onDrop={(event) => {
                                    event.preventDefault()
                                    if (draggingPanelId) movePanel(draggingPanelId, panelId)
                                    setDragOverPanelId(null)
                                    setDraggingPanelId(null)
                                }}
                            >
                                <TelemetryPanel
                                    panelId={panelId}
                                    sensors={sensors}
                                    title={`${panelTitleSeed} #${index + 1}`}
                                    onRemove={() => removePanel(panelId)}
                                    onSizeChange={(size) => handlePanelSizeChange(panelId, size)}
                                    dragHandleProps={{
                                        draggable: true,
                                        onDragStart: (event) => {
                                            setDraggingPanelId(panelId)
                                            event.dataTransfer.effectAllowed = 'move'
                                            event.dataTransfer.setData('text/plain', panelId)
                                        },
                                        onDragEnd: () => {
                                            setDraggingPanelId(null)
                                            setDragOverPanelId(null)
                                        },
                                    }}
                                />
                            </div>
                        )
                    })}
                </div>
            ) : (
                <div className="telemetry-view__history card">
                    <div className="telemetry-view__history-controls">
                        <div className="telemetry-view__history-sensors">
                            <div className="telemetry-view__sensor-filter">
                                <label htmlFor="telemetry_history_sensor_filter">Фильтр сенсоров</label>
                                <input
                                    id="telemetry_history_sensor_filter"
                                    type="text"
                                    className="telemetry-view__text-input"
                                    value={historySensorFilter}
                                    onChange={(event) => setHistorySensorFilter(event.target.value)}
                                    placeholder="Имя, тип, id"
                                />
                            </div>
                            <MaterialSelect
                                id="telemetry_history_sensors"
                                value=""
                                label="Сенсоры"
                                onChange={(value, event) => {
                                    addHistorySensor(value)
                                    if (event?.currentTarget) {
                                        event.currentTarget.value = ''
                                    }
                                }}
                                disabled={filteredHistorySensors.length === 0 || historySensorLimitReached}
                            >
                                <option value="">Добавить сенсор</option>
                                {filteredHistorySensors.map((sensor) => (
                                    <option key={sensor.id} value={sensor.id}>
                                        {sensor.name} ({sensor.type})
                                    </option>
                                ))}
                            </MaterialSelect>
                            <div className="telemetry-view__sensor-actions">
                                <button
                                    type="button"
                                    className="btn btn-secondary btn-xs"
                                    onClick={addAllHistorySensors}
                                    disabled={availableHistorySensors.length === 0 || historySensorLimitReached}
                                >
                                    Добавить все
                                </button>
                                <button
                                    type="button"
                                    className="btn btn-ghost btn-xs"
                                    onClick={clearHistorySensors}
                                    disabled={historySensorIds.length === 0}
                                >
                                    Очистить
                                </button>
                            </div>
                            <div className="telemetry-view__sensor-meta">
                                Выбрано {historySensorIds.length} / {HISTORY_MAX_SENSORS}
                            </div>
                            <div className="telemetry-view__sensor-list">
                                {historySensorIds.length === 0 && (
                                    <span className="telemetry-view__hint">Сенсоры не выбраны</span>
                                )}
                                {historySensorIds.map((id) => {
                                    const sensor = historySensorsById.get(id)
                                    return (
                                        <span key={id} className="telemetry-view__sensor-pill">
                                            {sensor?.name || id}
                                            <button type="button" onClick={() => removeHistorySensor(id)} aria-label="Удалить сенсор">
                                                ×
                                            </button>
                                        </span>
                                    )
                                })}
                            </div>
                        </div>

                        <div className="telemetry-view__history-options">
                            <label>
                                <span>max points</span>
                                <input
                                    type="number"
                                    min={100}
                                    max={20000}
                                    value={historyMaxPoints}
                                    onChange={(e) => setHistoryMaxPoints(Number(e.target.value || HISTORY_MAX_POINTS_DEFAULT))}
                                />
                            </label>
                            <label className="telemetry-view__checkbox">
                                <input
                                    type="checkbox"
                                    checked={historyIncludeLate}
                                    onChange={(e) => setHistoryIncludeLate(e.target.checked)}
                                    disabled={historyUseAggregated}
                                />
                                include late
                            </label>
                            <label className="telemetry-view__checkbox" title="Загрузить агрегированные данные (1-минутные бакеты: avg/min/max) вместо сырых точек">
                                <input
                                    type="checkbox"
                                    checked={historyUseAggregated}
                                    onChange={(e) => setHistoryUseAggregated(e.target.checked)}
                                />
                                агрегация 1m
                            </label>
                            <div className="telemetry-view__mode-toggle">
                                <label>
                                    <input
                                        type="radio"
                                        name="history-order"
                                        checked={historyOrder === 'asc'}
                                        onChange={() => setHistoryOrder('asc')}
                                    />
                                    от начала
                                </label>
                                <label>
                                    <input
                                        type="radio"
                                        name="history-order"
                                        checked={historyOrder === 'desc'}
                                        onChange={() => setHistoryOrder('desc')}
                                    />
                                    последние
                                </label>
                            </div>
                            <div className="telemetry-view__mode-toggle">
                                <label>
                                    <input
                                        type="radio"
                                        name="history-value-mode"
                                        checked={historyValueMode === 'physical'}
                                        onChange={() => setHistoryValueMode('physical')}
                                    />
                                    physical
                                </label>
                                <label>
                                    <input
                                        type="radio"
                                        name="history-value-mode"
                                        checked={historyValueMode === 'raw'}
                                        onChange={() => setHistoryValueMode('raw')}
                                    />
                                    raw
                                </label>
                            </div>
                            <button
                                type="button"
                                className="btn btn-primary btn-sm"
                                onClick={loadHistory}
                                disabled={historyLoading || !historyCaptureSessionId || historySensorOverLimit}
                            >
                                {historyLoading ? 'Загрузка...' : 'Загрузить'}
                            </button>
                            <button
                                type="button"
                                className="btn btn-secondary btn-sm"
                                onClick={continueHistoryInLive}
                                disabled={historyLoading || historyEffectiveSensorIds.length === 0 || !historyLastTimestamp}
                                title="Переключиться в live и продолжить с последних точек истории"
                            >
                                Продолжить в live
                            </button>
                        </div>
                    </div>

                    {historyError && <div className="telemetry-view__error">{historyError}</div>}
                    {(historyLoadedCount > 0 || historyWasTruncated) && (
                        <div className="telemetry-view__history-summary">
                            {historyUseAggregated
                                ? `Загружено бакетов (1m): ${historyLoadedCount}`
                                : `Загружено точек: ${historyLoadedCount}`}
                            {historyWasTruncated &&
                                ` (показаны ${historyOrder === 'desc' ? 'последние' : 'первые'} ${historyDisplayMaxPoints})`}
                        </div>
                    )}

                    <div className="telemetry-view__history-chart">
                        <div ref={historyPlotRef} className="telemetry-view__plotly" />
                        {historyLoading && (
                            <div className="telemetry-view__history-loading">
                                Загрузка истории…
                                {historyLoadedCount > 0 && (
                                    <span>
                                        {' '}
                                        {historyLoadedCount} / {historyMaxPoints}
                                    </span>
                                )}
                            </div>
                        )}
                        {!historyHasData && !historyLoading && (
                            <div className="telemetry-view__empty">Нет данных — нажмите «Загрузить»</div>
                        )}
                    </div>
                </div>
            )}

            {isLiveMode && typeof document !== 'undefined' &&
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
