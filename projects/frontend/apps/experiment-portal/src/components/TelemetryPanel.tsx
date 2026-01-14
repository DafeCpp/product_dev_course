import { useEffect, useMemo, useRef, useState } from 'react'
import type { Sensor, TelemetryStreamRecord } from '../types'
import { telemetryApi } from '../api/client'
import { createSSEParser } from '../utils/sse'
import Modal from './Modal'
import { MaterialSelect } from './common'
import './TelemetryPanel.css'

type TelemetryPanelProps = {
    panelId: string
    title: string
    sensors: Sensor[]
    onRemove: () => void
}

type StreamStatus = 'idle' | 'connecting' | 'streaming' | 'error'
type ValueMode = 'physical' | 'raw'

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

function clamp(n: number, min: number, max: number) {
    return Math.max(min, Math.min(max, n))
}

function buildLinePath(values: number[], min: number, max: number, width: number, height: number) {
    if (values.length === 0) return ''
    const margin = { left: 42, right: 14, top: 12, bottom: 24 }
    const plotW = Math.max(1, width - margin.left - margin.right)
    const plotH = Math.max(1, height - margin.top - margin.bottom)
    const maxLen = Math.max(1, values.length)
    const stepX = maxLen === 1 ? 0 : plotW / (maxLen - 1)
    const span = max === min ? 1 : max - min

    const points = values.map((v, i) => {
        const x = margin.left + i * stepX
        const y = margin.top + ((max - v) / span) * plotH
        return [x, y] as const
    })

    const [x0, y0] = points[0]
    const rest = points.slice(1).map(([x, y]) => `L ${x.toFixed(2)} ${y.toFixed(2)}`).join(' ')
    return `M ${x0.toFixed(2)} ${y0.toFixed(2)} ${rest}`.trim()
}

function buildAlignedPaths(
    series: { id: string; values: number[] }[],
    width: number,
    height: number
) {
    const allValues = series.flatMap((s) => s.values)
    if (allValues.length === 0) return { min: 0, max: 0, paths: [] as string[] }
    const min = Math.min(...allValues)
    const max = Math.max(...allValues)
    const margin = { left: 42, right: 14, top: 12, bottom: 24 }
    const plotW = Math.max(1, width - margin.left - margin.right)
    const plotH = Math.max(1, height - margin.top - margin.bottom)
    const maxLen = Math.max(1, ...series.map((s) => s.values.length))
    const stepX = maxLen === 1 ? 0 : plotW / (maxLen - 1)
    const span = max === min ? 1 : max - min

    const paths = series.map((s) => {
        if (s.values.length === 0) return ''
        const offset = maxLen - s.values.length
        const points = s.values.map((v, i) => {
            const x = margin.left + (offset + i) * stepX
            const y = margin.top + ((max - v) / span) * plotH
            return [x, y] as const
        })
        const [x0, y0] = points[0]
        const rest = points.slice(1).map(([x, y]) => `L ${x.toFixed(2)} ${y.toFixed(2)}`).join(' ')
        return `M ${x0.toFixed(2)} ${y0.toFixed(2)} ${rest}`.trim()
    })

    return { min, max, paths }
}

export default function TelemetryPanel({ panelId, title, sensors, onRemove }: TelemetryPanelProps) {
    const [panelTitle, setPanelTitle] = useState(title)
    const [selectedSensorIds, setSelectedSensorIds] = useState<string[]>([])
    const [status, setStatus] = useState<StreamStatus>('idle')
    const [valueMode, setValueMode] = useState<ValueMode>('physical')
    const [maxPoints, setMaxPoints] = useState(200)
    const [error, setError] = useState<string | null>(null)
    const [recordsBySensor, setRecordsBySensor] = useState<Record<string, TelemetryStreamRecord[]>>({})
    const [showSettings, setShowSettings] = useState(false)

    const abortControllers = useRef<Record<string, AbortController>>({})
    const runningRef = useRef<Record<string, boolean>>({})

    useEffect(() => {
        if (!panelTitle) setPanelTitle(title)
    }, [title, panelTitle])

    useEffect(() => {
        setSelectedSensorIds((prev) => prev.filter((id) => sensors.some((sensor) => sensor.id === id)))
    }, [sensors])

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

    const addSensor = (sensorId: string) => {
        if (!sensorId) return
        setSelectedSensorIds((prev) => (prev.includes(sensorId) ? prev : [...prev, sensorId]))
    }

    const removeSensor = (sensorId: string) => {
        setSelectedSensorIds((prev) => prev.filter((id) => id !== sensorId))
    }

    const stopStreams = () => {
        Object.values(abortControllers.current).forEach((controller) => controller.abort())
        abortControllers.current = {}
        runningRef.current = {}
        setStatus('idle')
    }

    const startStreamForSensor = async (sensorId: string) => {
        const abort = new AbortController()
        abortControllers.current[sensorId] = abort
        runningRef.current[sensorId] = true

        try {
            const { response: resp } = await telemetryApi.stream({
                sensor_id: sensorId,
                since_id: 0,
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
                        next[sensorId] = updated.length > maxPoints ? updated.slice(updated.length - maxPoints) : updated
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

        selectedSensorIds.forEach((sensorId) => {
            startStreamForSensor(sensorId)
        })

        setStatus('streaming')
    }

    const filteredSensors = useMemo(() => {
        const byId = new Map(sensors.map((s) => [s.id, s]))
        return selectedSensorIds.map((id) => byId.get(id)).filter(Boolean) as Sensor[]
    }, [selectedSensorIds, sensors])

    const seriesData = useMemo(() => {
        return filteredSensors.map((sensor) => {
            const records = recordsBySensor[sensor.id] || []
            const values = records
                .map((r) => (valueMode === 'physical' ? r.physical_value : r.raw_value))
                .filter((v): v is number => typeof v === 'number' && Number.isFinite(v))
                .slice(-50)
            return { id: sensor.id, name: sensor.name, values }
        })
    }, [filteredSensors, recordsBySensor, valueMode])

    const chartWidth = 640
    const chartHeight = 190
    const { min, max, paths } = useMemo(() => buildAlignedPaths(seriesData, chartWidth, chartHeight), [seriesData])

    const canStart = selectedSensorIds.length > 0 && status !== 'connecting'
    const availableSensors = sensors.filter((s) => !selectedSensorIds.includes(s.id))

    return (
        <div className="telemetry-panel card" data-panel-id={panelId}>
            <div className="telemetry-panel__header">
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
                <span className="telemetry-panel__meta-item">max points: {maxPoints}</span>
            </div>

            {error && <div className="telemetry-panel__error">{error}</div>}

            <div className="telemetry-panel__chart">
                <svg
                    className="telemetry-panel__svg"
                    width="100%"
                    height="190"
                    viewBox={`0 0 ${chartWidth} ${chartHeight}`}
                    role="img"
                    aria-label="Telemetry chart"
                    preserveAspectRatio="none"
                >
                    <rect x="0" y="0" width={chartWidth} height={chartHeight} className="telemetry-panel__bg" />
                    <g className="telemetry-panel__grid">
                        <line x1="42" y1="12" x2="42" y2="166" />
                        <line x1="42" y1="166" x2="626" y2="166" />
                        <line x1="42" y1="12" x2="626" y2="12" />
                        <line x1="42" y1={(12 + (166 - 12) * 0.33).toFixed(2)} x2="626" y2={(12 + (166 - 12) * 0.33).toFixed(2)} />
                        <line x1="42" y1={(12 + (166 - 12) * 0.66).toFixed(2)} x2="626" y2={(12 + (166 - 12) * 0.66).toFixed(2)} />
                    </g>
                    {paths.length === 0 ? (
                        <g className="telemetry-panel__empty">
                            <text x="50%" y="50%" dominantBaseline="middle" textAnchor="middle">
                                Нет данных — нажмите «Старт»
                            </text>
                        </g>
                    ) : (
                        paths.map((path, index) => (
                            <path
                                key={`${seriesData[index]?.id || index}-path`}
                                d={path || buildLinePath([], min, max, chartWidth, chartHeight)}
                                stroke={SERIES_COLORS[index % SERIES_COLORS.length]}
                                className="telemetry-panel__line"
                            />
                        ))
                    )}
                </svg>

                <div className="telemetry-panel__legend">
                    {seriesData.map((series, index) => (
                        <span key={series.id} className="telemetry-panel__legend-item">
                            <span
                                className="telemetry-panel__legend-dot"
                                style={{ background: SERIES_COLORS[index % SERIES_COLORS.length] }}
                            />
                            {series.name}
                            {series.values.length > 0 && (
                                <span className="telemetry-panel__legend-value">
                                    last={series.values[series.values.length - 1]?.toFixed(3)}
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
                        <div className="telemetry-panel__sensor-picker">
                            <MaterialSelect
                                id={`telemetry_panel_sensor_${panelId}`}
                                value=""
                                onChange={(value, event) => {
                                    addSensor(value)
                                    event.currentTarget.value = ''
                                }}
                                disabled={availableSensors.length === 0}
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
                            <label>max points</label>
                            <input
                                type="number"
                                value={maxPoints}
                                min={50}
                                max={2000}
                                onChange={(e) => setMaxPoints(clamp(Number(e.target.value || 200), 50, 2000))}
                            />
                        </div>
                    </div>
                </div>
            </Modal>
        </div>
    )
}
