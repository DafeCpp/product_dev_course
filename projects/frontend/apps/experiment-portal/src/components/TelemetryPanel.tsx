import { useEffect, useMemo, useRef, useState } from 'react'
import Plotly from 'plotly.js-dist-min'
import type { Sensor, TelemetryStreamRecord } from '../types'
import { telemetryApi } from '../api/client'
import { createSSEParser } from '../utils/sse'
import { MaterialSelect, PlayCircleIcon, StopCircleIcon, SettingsIcon, XIcon } from './common'
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
    onRecordReceived?: (sensorId: string, value: number) => void
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
    onRecordReceived,
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
    const settingsPopoverRef = useRef<HTMLDivElement | null>(null)
    const [panelSize, setPanelSize] = useState<{ width: number; height: number } | null>(null)
    const [startFromTimestamp, setStartFromTimestamp] = useState<string | null>(null)
    const [startFromCursorBySensor, setStartFromCursorBySensor] = useState<Record<string, StreamCursor> | null>(
        null
    )

    const abortControllers = useRef<Record<string, AbortController>>({})
    const runningRef = useRef<Record<string, boolean>>({})
    const plotRef = useRef<HTMLDivElement | null>(null)
    const scrollAnimRef = useRef<number | null>(null)
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
                    const val = valueMode === 'physical' ? parsed.physical_value : parsed.raw_value
                    if (typeof val === 'number' && Number.isFinite(val)) {
                        onRecordReceived?.(sensorId, val)
                    }
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

    const plotlyLayout = useMemo(() => {
        const isStreaming = status === 'streaming'
        const nowMs = Date.now()
        const windowMs = timeWindowSeconds * 1000

        return {
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
                ...(isStreaming
                    ? {
                          range: [
                              new Date(nowMs - windowMs).toISOString(),
                              new Date(nowMs).toISOString(),
                          ],
                          autorange: false,
                      }
                    : { autorange: true }),
            },
            yaxis: {
                showgrid: true,
                zeroline: false,
                gridcolor: 'rgba(15, 23, 42, 0.12)',
                tickfont: { size: 10, color: '#475569' },
                ticks: 'outside' as const,
                tickcolor: 'rgba(15, 23, 42, 0.12)',
            },
        }
    }, [status, timeWindowSeconds])

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
        const panel = panelRef.current
        if (!element || typeof ResizeObserver === 'undefined') return
        const observer = new ResizeObserver(() => {
            Plotly.Plots.resize(element)
        })
        observer.observe(element)
        if (panel) observer.observe(panel)
        return () => observer.disconnect()
    }, [])

    // ---- Smooth x-axis scroll during live streaming ----
    useEffect(() => {
        if (status !== 'streaming') {
            // When streaming stops, restore auto-range so the chart fits the data
            if (scrollAnimRef.current !== null) {
                cancelAnimationFrame(scrollAnimRef.current)
                scrollAnimRef.current = null
            }
            const element = plotRef.current
            if (element && hasData) {
                Plotly.relayout(element, { 'xaxis.autorange': true })
            }
            return
        }

        const element = plotRef.current
        if (!element) return

        const FRAME_INTERVAL_MS = 50 // ~20 fps
        let lastFrame = 0

        const animate = (timestamp: number) => {
            if (timestamp - lastFrame >= FRAME_INTERVAL_MS) {
                lastFrame = timestamp
                const nowMs = Date.now()
                const windowMs = timeWindowSeconds * 1000
                const startIso = new Date(nowMs - windowMs).toISOString()
                const endIso = new Date(nowMs).toISOString()
                Plotly.relayout(element, {
                    'xaxis.range': [startIso, endIso],
                    'xaxis.autorange': false,
                })
            }
            scrollAnimRef.current = requestAnimationFrame(animate)
        }

        scrollAnimRef.current = requestAnimationFrame(animate)

        return () => {
            if (scrollAnimRef.current !== null) {
                cancelAnimationFrame(scrollAnimRef.current)
                scrollAnimRef.current = null
            }
        }
    }, [status, timeWindowSeconds, hasData])

    useEffect(() => {
        return () => {
            const element = plotRef.current
            if (element) Plotly.purge(element)
            if (scrollAnimRef.current !== null) cancelAnimationFrame(scrollAnimRef.current)
        }
    }, [])

    useEffect(() => {
        if (!showSettings) return
        const handleClickOutside = (e: MouseEvent) => {
            if (settingsPopoverRef.current && !settingsPopoverRef.current.contains(e.target as Node)) {
                setShowSettings(false)
            }
        }
        document.addEventListener('mousedown', handleClickOutside)
        return () => document.removeEventListener('mousedown', handleClickOutside)
    }, [showSettings])

    const canStart = selectedSensorIds.length > 0 && status !== 'connecting'
    const availableSensors = sensors.filter((s) => !selectedSensorIds.includes(s.id))

    const formatWindowCompact = (seconds: number) => {
        if (seconds < 60) return `${seconds}с`
        return `${seconds / 60}м`
    }

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
                    {status !== 'streaming' ? (
                        <button className="tp-pill tp-pill--primary" onClick={startStreams} disabled={!canStart}>
                            <PlayCircleIcon />
                            {status === 'connecting' ? 'Подключение…' : 'Старт'}
                        </button>
                    ) : (
                        <button className="tp-pill" onClick={stopStreams}>
                            <StopCircleIcon />
                            Стоп
                        </button>
                    )}
                    <div className="tp-settings-wrap" ref={settingsPopoverRef}>
                        <button
                            className="tp-pill"
                            aria-expanded={showSettings}
                            onClick={() => setShowSettings((p) => !p)}
                        >
                            <SettingsIcon />
                            Настройки
                        </button>
                        {showSettings && (
                            <div className="tp-settings-popover" role="dialog" aria-label="Настройки панели">
                                <span className="tp-sp-arrow" />
                                <div className="tp-sp-title">Сенсоры</div>
                                {sensorsLoading && (
                                    <p className="telemetry-panel__hint telemetry-panel__hint--loading">Загрузка списка сенсоров…</p>
                                )}
                                {!sensorsLoading && sensorsError && (
                                    <p className="telemetry-panel__hint telemetry-panel__hint--error">{sensorsError}</p>
                                )}
                                {!sensorsLoading && !sensorsError && sensors.length === 0 && (
                                    <p className="telemetry-panel__hint telemetry-panel__hint--empty">
                                        Выберите проект с сенсорами в фильтрах выше.
                                    </p>
                                )}
                                <div className="telemetry-panel__sensor-picker">
                                    <MaterialSelect
                                        id={`telemetry_panel_sensor_${panelId}`}
                                        value=""
                                        onChange={(value, event) => {
                                            addSensor(value)
                                            if (event?.currentTarget) event.currentTarget.value = ''
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
                                        {selectedSensorIds.length === 0 && (
                                            <span className="telemetry-panel__hint">Сенсоры не выбраны</span>
                                        )}
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
                        )}
                    </div>
                    <button className="tp-pill tp-pill--danger" onClick={onRemove} aria-label="Удалить панель" title="Удалить панель">
                        <XIcon />
                    </button>
                </div>
            </div>

            <div className="tp-settings-tile">
                <div className="tp-seg">
                    {TIME_WINDOWS_SECONDS.map((seconds) => (
                        <label key={seconds} className={timeWindowSeconds === seconds ? 'on' : ''}>
                            <input
                                type="radio"
                                name={`tp-win-${panelId}`}
                                checked={timeWindowSeconds === seconds}
                                onChange={() => setTimeWindowSeconds(seconds as TimeWindowSeconds)}
                            />
                            <span>{formatWindowCompact(seconds)}</span>
                        </label>
                    ))}
                </div>
                <span className="tp-settings-tile__divider" />
                <div className="tp-seg">
                    <label className={valueMode === 'physical' ? 'on' : ''}>
                        <input type="radio" name={`tp-vm-${panelId}`} checked={valueMode === 'physical'} onChange={() => setValueMode('physical')} />
                        <span>physical</span>
                    </label>
                    <label className={valueMode === 'raw' ? 'on' : ''}>
                        <input type="radio" name={`tp-vm-${panelId}`} checked={valueMode === 'raw'} onChange={() => setValueMode('raw')} />
                        <span>raw</span>
                    </label>
                </div>
                <label className="tp-toggle" title="Якорить окно на последних данных">
                    <input
                        type="checkbox"
                        checked={useLatestAnchor}
                        onChange={(e) => setUseLatestAnchor(e.target.checked)}
                    />
                    <span>последние</span>
                </label>
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

        </div>
    )
}
