import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { useTelemetryStream, type TelemetryStreamOpener, type TelemetryStreamStatus } from 'frontend-common'
import type { Sensor, TelemetryStreamRecord } from '../types'
import { telemetryApi } from '../api/client'
import { MaterialSelect, PlayCircleIcon, StopCircleIcon, SettingsIcon, XIcon } from './common'
import { getStreamErrorMessage, streamResponseError } from '../utils/streamError'
import LiveTelemetryChart, { MAX_POINTS_CAP } from './telemetry/LiveTelemetryChart'
import { createTelemetryRecordsStore } from './telemetry/telemetryRecordsStore'
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

const TIME_WINDOWS_SECONDS = [5, 10, 30, 60, 120, 300, 600] as const

type SensorStreamInitialCursor = { sinceTs?: string; sinceId?: number }

/**
 * Owns one useTelemetryStream instance for a single sensor. Mounted only
 * while the panel is running (see runningSensorIds) — hooks can't be called
 * in a loop, so each running sensor gets its own instance via this seam.
 */
function SensorStream({
    sensorId,
    initialCursor,
    valueMode,
    onRecords,
    onStatusChange,
    onRecordReceived,
    onStreamError,
}: {
    sensorId: string
    initialCursor: SensorStreamInitialCursor
    valueMode: ValueMode
    onRecords: (sensorId: string, points: TelemetryStreamRecord[]) => void
    onStatusChange: (sensorId: string, status: TelemetryStreamStatus) => void
    onRecordReceived?: (sensorId: string, value: number) => void
    onStreamError: (sensorId: string, error: Error) => void
}) {
    const open: TelemetryStreamOpener = useCallback(
        async ({ sinceTs, sinceId, idleTimeoutSeconds, signal }) => {
            const { response } = await telemetryApi.stream({
                sensor_id: sensorId,
                since_ts: sinceTs,
                since_id: sinceId,
                idle_timeout_seconds: idleTimeoutSeconds,
                signal,
            })
            if (!response.ok) throw await streamResponseError(response)
            if (!response.body) throw new Error('Stream body is empty')
            return { response }
        },
        [sensorId],
    )

    const onError = useCallback(
        (err: Error, ctx: { willRetry: boolean }) => {
            if (!ctx.willRetry) onStreamError(sensorId, err)
        },
        [sensorId, onStreamError],
    )

    const stream = useTelemetryStream(sensorId, {
        open,
        bufferSize: MAX_POINTS_CAP,
        idleTimeoutSeconds: 30,
        initialSinceTs: initialCursor.sinceTs,
        initialSinceId: initialCursor.sinceId,
        onError,
    })

    useEffect(() => {
        stream.start()
        // Mount-only: this component is mounted exactly while it should be running.
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [])

    useEffect(() => {
        onStatusChange(sensorId, stream.status)
    }, [sensorId, stream.status, onStatusChange])

    useEffect(() => {
        onRecords(sensorId, stream.points)
    }, [sensorId, stream.points, onRecords])

    const valueModeRef = useRef(valueMode)
    valueModeRef.current = valueMode

    const lastRecord = stream.lastRecord
    useEffect(() => {
        // Deliberately excludes valueMode from deps: this must fire only when a
        // new record actually arrives, not on every physical/raw toggle (which
        // would re-emit the same record and duplicate points downstream).
        if (!lastRecord) return
        const val = valueModeRef.current === 'physical' ? lastRecord.physical_value : lastRecord.raw_value
        if (typeof val === 'number' && Number.isFinite(val)) onRecordReceived?.(sensorId, val)
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [sensorId, lastRecord, onRecordReceived])

    return null
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
    const [valueMode, setValueMode] = useState<ValueMode>('physical')
    const [timeWindowSeconds, setTimeWindowSeconds] = useState<TimeWindowSeconds>(300)
    const [useLatestAnchor, setUseLatestAnchor] = useState(true)
    const [error, setError] = useState<string | null>(null)
    const [recordsStore] = useState(createTelemetryRecordsStore)
    const [runningSensorIds, setRunningSensorIds] = useState<string[]>([])
    const [statusBySensor, setStatusBySensor] = useState<Record<string, TelemetryStreamStatus>>({})
    const [showSettings, setShowSettings] = useState(false)
    const settingsPopoverRef = useRef<HTMLDivElement | null>(null)
    const [panelSize, setPanelSize] = useState<{ width: number; height: number } | null>(null)
    const [startFromTimestamp, setStartFromTimestamp] = useState<string | null>(null)
    const [startFromCursorBySensor, setStartFromCursorBySensor] = useState<Record<string, StreamCursor> | null>(
        null
    )

    const initialCursorsRef = useRef<Record<string, SensorStreamInitialCursor>>({})
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
        recordsStore.retain(selectedSensorIds)
    }, [recordsStore, selectedSensorIds])

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
        setRunningSensorIds([])
        setStatusBySensor({})
    }

    const onSensorRecords = useCallback(
        (sensorId: string, points: TelemetryStreamRecord[]) => recordsStore.set(sensorId, points),
        [recordsStore],
    )

    const onSensorStatusChange = useCallback((sensorId: string, sensorStatus: TelemetryStreamStatus) => {
        setStatusBySensor((prev) => (prev[sensorId] === sensorStatus ? prev : { ...prev, [sensorId]: sensorStatus }))
    }, [])

    const onSensorStreamError = useCallback((_sensorId: string, err: Error) => {
        setError(getStreamErrorMessage(err))
    }, [])

    const startStreams = () => {
        setError(null)
        recordsStore.clear()
        setStatusBySensor({})
        const defaultSinceTs = startFromTimestamp ?? new Date().toISOString()

        const cursors: Record<string, SensorStreamInitialCursor> = {}
        selectedSensorIds.forEach((sensorId) => {
            const cursor = startFromCursorBySensor?.[sensorId]
            cursors[sensorId] = {
                sinceTs: cursor?.timestamp ?? defaultSinceTs,
                sinceId: cursor?.id ?? 0,
            }
        })
        initialCursorsRef.current = cursors
        // Snapshot which sensors are running for this start; sensors added to
        // selectedSensorIds afterwards only join on the next explicit Старт click.
        setRunningSensorIds([...selectedSensorIds])

        if (startFromTimestamp) setStartFromTimestamp(null)
        if (startFromCursorBySensor) setStartFromCursorBySensor(null)
    }

    const status: StreamStatus = useMemo(() => {
        if (runningSensorIds.length === 0) return 'idle'
        const statuses = runningSensorIds.map((id) => statusBySensor[id])
        if (statuses.some((s) => s === 'streaming' || s === 'reconnecting')) return 'streaming'
        if (statuses.some((s) => s === 'error') && statuses.every((s) => s === 'error' || s === 'stopped')) {
            return 'error'
        }
        return 'connecting'
    }, [runningSensorIds, statusBySensor])

    const filteredSensors = useMemo(() => {
        const byId = new Map(sensors.map((s) => [s.id, s]))
        return selectedSensorIds.map((id) => byId.get(id)).filter(Boolean) as Sensor[]
    }, [selectedSensorIds, sensors])

    useEffect(() => {
        if (!showSettings) return
        const handleClickOutside = (e: MouseEvent) => {
            if (settingsPopoverRef.current && !settingsPopoverRef.current.contains(e.target as Node)) {
                setShowSettings(false)
            }
        }
        // Use 'click' (not 'mousedown') so that clicks on the portaled MaterialSelect
        // dropdown (rendered in document.body) fire the option's onClick before closing
        // this popover. With 'mousedown' the popover unmounts before 'click' fires.
        document.addEventListener('click', handleClickOutside)
        return () => document.removeEventListener('click', handleClickOutside)
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
            {runningSensorIds.map((sensorId) => (
                <SensorStream
                    key={sensorId}
                    sensorId={sensorId}
                    initialCursor={initialCursorsRef.current[sensorId] ?? {}}
                    valueMode={valueMode}
                    onRecords={onSensorRecords}
                    onStatusChange={onSensorStatusChange}
                    onRecordReceived={onRecordReceived}
                    onStreamError={onSensorStreamError}
                />
            ))}
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

            {error && <div className="telemetry-panel__error" role="alert">{error}</div>}

            <LiveTelemetryChart
                store={recordsStore}
                sensors={filteredSensors}
                valueMode={valueMode}
                timeWindowSeconds={timeWindowSeconds}
                useLatestAnchor={useLatestAnchor}
                streaming={status === 'streaming'}
            />

        </div>
    )
}
