import { useEffect, useMemo, useRef, useState } from 'react'
import Plotly from 'plotly.js-dist-min'
import type { Sensor, TelemetryStreamRecord } from '../types'
import { telemetryApi } from '../api/client'
import { createSSEParser } from '../utils/sse'
import Modal from './Modal'
import { MaterialSelect } from './common'
import './TelemetryPanel.scss'

type TelemetryPanelProps = {
    panelId: string
    title: string
    sensors: Sensor[]
    /** True when sensors are still loading (project-scoped query in progress). */
    sensorsLoading?: boolean
    /** Error message from sensors query, if any. */
    sensorsError?: string | null
    onRemove: () => void
    dragHandleProps?: React.ButtonHTMLAttributes<HTMLButtonElement>
    onSizeChange?: (size: { width: number; height: number }) => void
}

type StreamStatus = 'idle' | 'connecting' | 'streaming' | 'error'
type ValueMode = 'physical' | 'raw'
type TimeWindowSeconds = 5 | 10 | 30 | 60 | 120 | 300 | 600
type StreamCursor = { timestamp: string; id: number }

type TelemetryPanelState = {
    title: string
    selectedSensorIds: string[]
    valueMode: ValueMode
    /** @deprecated kept for backward compat with localStorage; no longer shown in UI */
    maxPoints?: number
    timeWindowSeconds: TimeWindowSeconds
    useLatestAnchor: boolean
    startFromTimestamp?: string
    startFromCursorBySensor?: Record<string, StreamCursor>
    size?: { width: number; height: number }
}

/** Internal safety cap — prevents chart from rendering too many DOM elements. */
const MAX_POINTS_CAP = 5000

const SERIES_COLORS = [
    '#2563eb',
    '#16a34a',
    '#f97316',
    '#a855f7',
    '#06b6d4',
    '#e11d48',
    '#ca8a04',
    '#0ea5e9',
]
const TIME_WINDOWS_SECONDS = [5, 10, 30, 60, 120, 300, 600] as const

function formatWindow(seconds: number) {
    if (seconds < 60) return `${seconds} сек`
    if (seconds % 60 === 0) return `${seconds / 60} мин`
    return `${seconds} сек`
}

function parseTimestampMs(value: string) {
    const parsed = Date.parse(value)
    return Number.isFinite(parsed) ? parsed : null
}


export default function TelemetryPanel({
    panelId,
    title,
    sensors,
    sensorsLoading = false,
    sensorsError = null,
    onRemove,
    dragHandleProps,
    onSizeChange,
}: TelemetryPanelProps) {
    const [panelTitle, setPanelTitle] = useState(title)
    const [selectedSensorIds, setSelectedSensorIds] = useState<string[]>([])
    const [status, setStatus] = useState<StreamStatus>('idle')
    const [valueMode, setValueMode] = useState<ValueMode>('physical')
    const [timeWindowSeconds, setTimeWindowSeconds] = useState<TimeWindowSeconds>(300)
    const [useLatestAnchor, setUseLatestAnchor] = useState(true)
    const [error, setError] = useState<string | null>(null)
    const [recordsBySensor, setRecordsBySensor] = useState<Record<string, TelemetryStreamRecord[]>>({})
    const [showSettings, setShowSettings] = useState(false)
    const [panelSize, setPanelSize] = useState<{ width: number; height: number } | null>(null)
    const [startFromTimestamp, setStartFromTimestamp] = useState<string | null>(null)
    const [startFromCursorBySensor, setStartFromCursorBySensor] = useState<Record<string, StreamCursor> | null>(
        null
    )

    const abortControllers = useRef<Record<string, AbortController>>({})
    const runningRef = useRef<Record<string, boolean>>({})
    const plotRef = useRef<HTMLDivElement | null>(null)
    const panelRef = useRef<HTMLDivElement | null>(null)
    const stateLoadedRef = useRef(false)
    const skipInitialSaveRef = useRef(true)

    const storageKey = `telemetry_panel_state_${panelId}`

    const persistPanelState = (nextSelectedSensorIds?: string[]) => {
        if (typeof window === 'undefined') return
        const payload: TelemetryPanelState = {
            title: panelTitle,
            selectedSensorIds: nextSelectedSensorIds ?? selectedSensorIds,
            valueMode,
            timeWindowSeconds,
            useLatestAnchor,
            startFromTimestamp: startFromTimestamp ?? undefined,
            startFromCursorBySensor: startFromCursorBySensor ?? undefined,
            size: panelSize ?? undefined,
        }
        window.localStorage.setItem(storageKey, JSON.stringify(payload))
        console.debug('[TelemetryPanel] saved state (manual)', { panelId, storageKey, payload })
    }

    useEffect(() => {
        if (typeof window === 'undefined') return
        const raw = window.localStorage.getItem(storageKey)
        if (!raw) {
            console.debug('[TelemetryPanel] no saved state', { panelId, storageKey })
            stateLoadedRef.current = true
            skipInitialSaveRef.current = true
            return
        }
        try {
            const parsed = JSON.parse(raw) as Partial<TelemetryPanelState>
            console.debug('[TelemetryPanel] loaded state', { panelId, storageKey, parsed })
            if (typeof parsed.title === 'string') setPanelTitle(parsed.title)
            if (Array.isArray(parsed.selectedSensorIds)) {
                const next = parsed.selectedSensorIds.filter((id) => typeof id === 'string')
                console.debug('[TelemetryPanel] restore selected sensors', { panelId, next })
                setSelectedSensorIds(next)
            }
            if (parsed.valueMode === 'physical' || parsed.valueMode === 'raw') setValueMode(parsed.valueMode)
            if (
                typeof parsed.timeWindowSeconds === 'number' &&
                TIME_WINDOWS_SECONDS.includes(parsed.timeWindowSeconds as TimeWindowSeconds)
            ) {
                setTimeWindowSeconds(parsed.timeWindowSeconds as TimeWindowSeconds)
            } else if (typeof (parsed as { timeWindowMinutes?: number }).timeWindowMinutes === 'number') {
                const seconds = (parsed as { timeWindowMinutes: number }).timeWindowMinutes * 60
                if (TIME_WINDOWS_SECONDS.includes(seconds as TimeWindowSeconds)) {
                    setTimeWindowSeconds(seconds as TimeWindowSeconds)
                }
            }
            if (typeof parsed.useLatestAnchor === 'boolean') {
                setUseLatestAnchor(parsed.useLatestAnchor)
            }
            if (typeof parsed.startFromTimestamp === 'string') {
                setStartFromTimestamp(parsed.startFromTimestamp)
            }
            if (parsed.startFromCursorBySensor && typeof parsed.startFromCursorBySensor === 'object') {
                const next: Record<string, StreamCursor> = {}
                Object.entries(parsed.startFromCursorBySensor as Record<string, unknown>).forEach(([sensorId, cursor]) => {
                    if (!cursor || typeof cursor !== 'object') return
                    const typed = cursor as { timestamp?: unknown; id?: unknown }
                    if (typeof typed.timestamp !== 'string') return
                    if (typeof typed.id !== 'number' || !Number.isFinite(typed.id)) return
                    next[sensorId] = { timestamp: typed.timestamp, id: Math.floor(typed.id) }
                })
                if (Object.keys(next).length > 0) {
                    setStartFromCursorBySensor(next)
                }
            }
            if (
                parsed.size &&
                typeof parsed.size.width === 'number' &&
                typeof parsed.size.height === 'number'
            ) {
                setPanelSize({
                    width: Math.max(280, Math.round(parsed.size.width)),
                    height: Math.max(240, Math.round(parsed.size.height)),
                })
            }
        } catch {
            // ignore malformed local storage
        } finally {
            stateLoadedRef.current = true
            skipInitialSaveRef.current = true
        }
    }, [storageKey])

    useEffect(() => {
        if (!panelTitle) setPanelTitle(title)
    }, [title, panelTitle])

    useEffect(() => {
        if (sensors.length === 0) return
        setSelectedSensorIds((prev) => {
            const next = prev.filter((id) => sensors.some((sensor) => sensor.id === id))
            if (next.length !== prev.length) {
                console.debug('[TelemetryPanel] filtered sensors', {
                    panelId,
                    before: prev,
                    after: next,
                    available: sensors.map((sensor) => sensor.id),
                })
            }
            return next
        })
    }, [sensors])

    useEffect(() => {
        console.debug('[TelemetryPanel] selected sensors state', { panelId, selectedSensorIds })
    }, [panelId, selectedSensorIds])

    useEffect(() => {
        if (typeof window === 'undefined' || !stateLoadedRef.current) return
        if (skipInitialSaveRef.current) {
            skipInitialSaveRef.current = false
            console.debug('[TelemetryPanel] skip initial save', { panelId, storageKey })
            return
        }
        const payload: TelemetryPanelState = {
            title: panelTitle,
            selectedSensorIds,
            valueMode,
            timeWindowSeconds,
            useLatestAnchor,
            startFromTimestamp: startFromTimestamp ?? undefined,
            startFromCursorBySensor: startFromCursorBySensor ?? undefined,
            size: panelSize ?? undefined,
        }
        window.localStorage.setItem(storageKey, JSON.stringify(payload))
        console.debug('[TelemetryPanel] saved state', { panelId, storageKey, payload })
    }, [
        panelTitle,
        selectedSensorIds,
        valueMode,
        timeWindowSeconds,
        useLatestAnchor,
        startFromTimestamp,
        startFromCursorBySensor,
        panelSize,
        storageKey,
    ])

    useEffect(() => {
        if (panelSize) onSizeChange?.(panelSize)
    }, [panelSize, onSizeChange])

    useEffect(() => {
        setRecordsBySensor((prev) => {
            const next: Record<string, TelemetryStreamRecord[]> = {}
            selectedSensorIds.forEach((id) => {
                if (prev[id]) next[id] = prev[id]
            })
            return next
        })
    }, [selectedSensorIds])

    useEffect(() => {
        return () => {
            Object.values(abortControllers.current).forEach((controller) => controller.abort())
            abortControllers.current = {}
            runningRef.current = {}
        }
    }, [])

    useEffect(() => {
        const element = panelRef.current
        if (!element || typeof ResizeObserver === 'undefined') return

        const updateSize = () => {
            const rect = element.getBoundingClientRect()
            const width = Math.max(280, Math.round(rect.width))
            const height = Math.max(240, Math.round(rect.height))
            setPanelSize((prev) => (prev && prev.width === width && prev.height === height ? prev : { width, height }))
        }

        updateSize()
        const observer = new ResizeObserver(updateSize)
        observer.observe(element)

        return () => observer.disconnect()
    }, [])

    const addSensor = (sensorId: string) => {
        if (!sensorId) return
        console.debug('[TelemetryPanel] add sensor', { panelId, sensorId })
        setSelectedSensorIds((prev) => {
            if (prev.includes(sensorId)) return prev
            const next = [...prev, sensorId]
            persistPanelState(next)
            return next
        })
    }

    const removeSensor = (sensorId: string) => {
        setSelectedSensorIds((prev) => {
            const next = prev.filter((id) => id !== sensorId)
            persistPanelState(next)
            return next
        })
    }

    const stopStreams = () => {
        Object.values(abortControllers.current).forEach((controller) => controller.abort())
        abortControllers.current = {}
        runningRef.current = {}
        setStatus('idle')
    }

    const startStreamForSensor = async (sensorId: string, sinceTs: string, sinceId: number) => {
        const abort = new AbortController()
        abortControllers.current[sensorId] = abort
        runningRef.current[sensorId] = true

        try {
            const { response: resp } = await telemetryApi.stream({
                sensor_id: sensorId,
                since_ts: sinceTs,
                since_id: sinceId,
                idle_timeout_seconds: 30,
            })
            if (!resp.ok) {
                const text = await resp.text().catch(() => '')
                throw new Error(text || `Ошибка стрима: HTTP ${resp.status}`)
            }
            if (!resp.body) throw new Error('Stream body is empty')

            const reader = resp.body.getReader()
            const decoder = new TextDecoder()
            const parser = createSSEParser((evt) => {
                if (evt.event !== 'telemetry') return
                try {
                    const parsed = JSON.parse(evt.data) as TelemetryStreamRecord
                    setRecordsBySensor((prev) => {
                        const next = { ...prev }
                        const existing = next[sensorId] || []
                        const updated = [...existing, parsed]
                        next[sensorId] = updated.length > MAX_POINTS_CAP ? updated.slice(updated.length - MAX_POINTS_CAP) : updated
                        return next
                    })
                } catch (e: any) {
                    setError(e?.message || 'Ошибка парсинга telemetry event')
                    setStatus('error')
                }
            })

            while (runningRef.current[sensorId] && !abort.signal.aborted) {
                const { value, done } = await reader.read()
                if (done) break
                if (value) parser.feed(decoder.decode(value, { stream: true }))
            }
        } catch (e: any) {
            if (e?.name === 'AbortError') return
            setError(e?.message || 'Ошибка подключения к стриму')
            setStatus('error')
        } finally {
            runningRef.current[sensorId] = false
        }
    }

    const startStreams = () => {
        setError(null)
        setRecordsBySensor({})
        setStatus('connecting')
        const defaultSinceTs = startFromTimestamp ?? new Date().toISOString()

        selectedSensorIds.forEach((sensorId) => {
            const cursor = startFromCursorBySensor?.[sensorId]
            const sinceTs = cursor?.timestamp ?? defaultSinceTs
            const sinceId = cursor?.id ?? 0
            startStreamForSensor(sensorId, sinceTs, sinceId)
        })

        setStatus('streaming')
        if (startFromTimestamp) setStartFromTimestamp(null)
        if (startFromCursorBySensor) setStartFromCursorBySensor(null)
    }

    const filteredSensors = useMemo(() => {
        const byId = new Map(sensors.map((s) => [s.id, s]))
        return selectedSensorIds.map((id) => byId.get(id)).filter(Boolean) as Sensor[]
    }, [selectedSensorIds, sensors])

    const seriesData = useMemo(() => {
        const allRecords = filteredSensors.flatMap((sensor) => recordsBySensor[sensor.id] || [])
        const latestTimestampMs = allRecords.reduce((latest, record) => {
            const ts = parseTimestampMs(record.timestamp)
            if (ts === null) return latest
            return ts > latest ? ts : latest
        }, Number.NEGATIVE_INFINITY)
        const nowMs = Date.now()
        const windowMs = timeWindowSeconds * 1000
        const anchorMs = useLatestAnchor ? latestTimestampMs : nowMs
        const hasWindowAnchor = Number.isFinite(anchorMs) && anchorMs > 0

        return filteredSensors.map((sensor) => {
            const records = recordsBySensor[sensor.id] || []
            const points = records
                .map((r) => ({
                    x: r.timestamp,
                    y: valueMode === 'physical' ? r.physical_value : r.raw_value,
                    ts: parseTimestampMs(r.timestamp),
                }))
                .filter((point): point is { x: string; y: number; ts: number } => {
                    if (typeof point.y !== 'number' || !Number.isFinite(point.y)) return false
                    return typeof point.ts === 'number' && Number.isFinite(point.ts)
                })
                .filter((point) => {
                    if (!hasWindowAnchor) return true
                    return point.ts >= anchorMs - windowMs
                })
                .slice(-MAX_POINTS_CAP)
            return {
                id: sensor.id,
                name: sensor.name,
                x: points.map((point) => point.x),
                y: points.map((point) => point.y),
            }
        })
    }, [filteredSensors, recordsBySensor, valueMode, timeWindowSeconds, useLatestAnchor])

    const plotlyData = useMemo(
        () =>
            seriesData.map((series, index) => ({
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
            })),
        [seriesData]
    )

    const hasData = useMemo(() => seriesData.some((series) => series.y.length > 0), [seriesData])

    const plotlyLayout = useMemo(
        () => ({
            autosize: true,
            margin: { l: 42, r: 14, t: 12, b: 24 },
            showlegend: false,
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

    const plotlyConfig = useMemo(
        () => ({
            responsive: true,
            displayModeBar: false,
            displaylogo: false,
        }),
        []
    )

    useEffect(() => {
        const element = plotRef.current
        if (!element) return
        const data = hasData ? plotlyData : []
        Plotly.react(element, data, plotlyLayout, plotlyConfig)
    }, [plotlyData, plotlyLayout, plotlyConfig, hasData])

    useEffect(() => {
        const element = plotRef.current
        if (!element || typeof ResizeObserver === 'undefined') return
        const observer = new ResizeObserver(() => {
            Plotly.Plots.resize(element)
        })
        observer.observe(element)
        return () => observer.disconnect()
    }, [])

    useEffect(() => {
        return () => {
            const element = plotRef.current
            if (element) Plotly.purge(element)
        }
    }, [])

    const canStart = selectedSensorIds.length > 0 && status !== 'connecting'
    const availableSensors = sensors.filter((s) => !selectedSensorIds.includes(s.id))

    return (
        <div
            ref={panelRef}
            className="telemetry-panel card"
            data-panel-id={panelId}
            style={panelSize ? { width: panelSize.width, height: panelSize.height } : undefined}
        >
            <div className="telemetry-panel__header">
                <button
                    type="button"
                    className="telemetry-panel__drag-handle"
                    aria-label="Перетащить панель"
                    title="Перетащить панель"
                    {...dragHandleProps}
                >
                    ::
                </button>
                <input
                    className="telemetry-panel__title"
                    value={panelTitle}
                    onChange={(e) => setPanelTitle(e.target.value)}
                    placeholder="Название панели"
                />
                <div className="telemetry-panel__actions">
                    <button className="btn btn-secondary btn-sm" onClick={() => setShowSettings(true)}>
                        Настройки
                    </button>
                    {status !== 'streaming' ? (
                        <button className="btn btn-primary btn-sm" onClick={startStreams} disabled={!canStart}>
                            {status === 'connecting' ? 'Подключение...' : 'Старт'}
                        </button>
                    ) : (
                        <button className="btn btn-secondary btn-sm" onClick={stopStreams}>
                            Стоп
                        </button>
                    )}
                    <button className="btn btn-danger btn-sm" onClick={onRemove}>
                        Удалить
                    </button>
                </div>
            </div>

            <div className="telemetry-panel__meta">
                <span className="telemetry-panel__meta-item">
                    сенсоры: {selectedSensorIds.length || 'нет'}
                </span>
                <span className="telemetry-panel__meta-item">режим: {valueMode}</span>
                <span className="telemetry-panel__meta-item">окно: {formatWindow(timeWindowSeconds)}</span>
                <span className="telemetry-panel__meta-item">якорь: {useLatestAnchor ? 'последние данные' : 'сейчас'}</span>
            </div>

            {error && <div className="telemetry-panel__error">{error}</div>}

            <div className="telemetry-panel__chart">
                <div className="telemetry-panel__chart-area">
                    <div ref={plotRef} className="telemetry-panel__plotly" />
                    {!hasData && <div className="telemetry-panel__empty">Нет данных — нажмите «Старт»</div>}
                </div>

                <div className="telemetry-panel__legend">
                    {seriesData.map((series, index) => (
                        <span key={series.id} className="telemetry-panel__legend-item">
                            <span
                                className="telemetry-panel__legend-dot"
                                style={{ background: SERIES_COLORS[index % SERIES_COLORS.length] }}
                            />
                            {series.name}
                            {series.y.length > 0 && (
                                <span className="telemetry-panel__legend-value">
                                    last={series.y[series.y.length - 1]?.toFixed(3)}
                                </span>
                            )}
                        </span>
                    ))}
                </div>
            </div>

            <Modal
                isOpen={showSettings}
                onClose={() => setShowSettings(false)}
                title="Настройки панели"
                className="telemetry-panel__settings-modal"
            >
                <div className="telemetry-panel__settings">
                    <div className="form-group">
                        <label>Сенсоры</label>
                        {sensorsLoading && (
                            <p className="telemetry-panel__hint telemetry-panel__hint--loading">Загрузка списка сенсоров…</p>
                        )}
                        {!sensorsLoading && sensorsError && (
                            <p className="telemetry-panel__hint telemetry-panel__hint--error">{sensorsError}</p>
                        )}
                        {!sensorsLoading && !sensorsError && sensors.length === 0 && (
                            <p className="telemetry-panel__hint telemetry-panel__hint--empty">
                                Список сенсоров пуст. Выберите проект с сенсорами в фильтрах выше.
                            </p>
                        )}
                        <div className="telemetry-panel__sensor-picker">
                            <MaterialSelect
                                id={`telemetry_panel_sensor_${panelId}`}
                                value=""
                                onChange={(value, event) => {
                                    addSensor(value)
                                    if (event?.currentTarget) {
                                        event.currentTarget.value = ''
                                    }
                                }}
                                disabled={availableSensors.length === 0 || sensorsLoading}
                            >
                                <option value="">Добавить сенсор</option>
                                {availableSensors.map((sensor) => (
                                    <option key={sensor.id} value={sensor.id}>
                                        {sensor.name} ({sensor.type})
                                    </option>
                                ))}
                            </MaterialSelect>
                            <div className="telemetry-panel__sensor-list">
                                {selectedSensorIds.length === 0 && <span className="telemetry-panel__hint">Сенсоры не выбраны</span>}
                                {filteredSensors.map((sensor) => (
                                    <span key={sensor.id} className="telemetry-panel__sensor-pill">
                                        {sensor.name}
                                        <button type="button" onClick={() => removeSensor(sensor.id)} aria-label="Удалить сенсор">
                                            ×
                                        </button>
                                    </span>
                                ))}
                            </div>
                        </div>
                    </div>

                    <div className="telemetry-panel__settings-grid">
                        <div className="form-group telemetry-panel__inline">
                            <label>Значение</label>
                            <div className="telemetry-panel__mode">
                                <label>
                                    <input
                                        type="radio"
                                        name={`panel-mode-${panelId}`}
                                        checked={valueMode === 'physical'}
                                        onChange={() => setValueMode('physical')}
                                    />
                                    physical
                                </label>
                                <label>
                                    <input
                                        type="radio"
                                        name={`panel-mode-${panelId}`}
                                        checked={valueMode === 'raw'}
                                        onChange={() => setValueMode('raw')}
                                    />
                                    raw
                                </label>
                            </div>
                        </div>

                        <div className="form-group telemetry-panel__inline">
                            <label>Окно</label>
                            <MaterialSelect
                                id={`telemetry_panel_window_${panelId}`}
                                value={String(timeWindowSeconds)}
                                onChange={(value) => {
                                    const next = Number(value)
                                    if (TIME_WINDOWS_SECONDS.includes(next as TimeWindowSeconds)) {
                                        setTimeWindowSeconds(next as TimeWindowSeconds)
                                    }
                                }}
                            >
                                {TIME_WINDOWS_SECONDS.map((seconds) => (
                                    <option key={seconds} value={seconds}>
                                        {formatWindow(seconds)}
                                    </option>
                                ))}
                            </MaterialSelect>
                        </div>

                        <div className="form-group telemetry-panel__inline">
                            <label>Окно от</label>
                            <label className="telemetry-panel__checkbox">
                                <input
                                    type="checkbox"
                                    checked={useLatestAnchor}
                                    onChange={(e) => setUseLatestAnchor(e.target.checked)}
                                />
                                последних данных
                            </label>
                        </div>
                    </div>
                </div>
            </Modal>
        </div>
    )
}
