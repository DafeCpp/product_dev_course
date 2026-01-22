import { useEffect, useMemo, useRef, useState } from 'react'
import Modal from './Modal'
import { MaterialSelect } from './common'
import { telemetryApi } from '../api/client'
import { createSSEParser } from '../utils/sse'
import type { TelemetryStreamRecord } from '../types'
import { buildHttpDebugInfoFromFetch, maybeEmitHttpErrorToast, truncateString } from '../utils/httpDebug'
import { IS_TEST } from '../utils/env'
import { notifyError } from '../utils/notify'
import './TelemetryStreamModal.scss'

type TelemetryStreamModalProps = {
    sensorId: string
    isOpen: boolean
    onClose: () => void
    filterRunId?: string
    filterCaptureSessionId?: string
}

type StreamStatus = 'idle' | 'connecting' | 'streaming' | 'error'
type FilterMode = 'all' | 'run' | 'capture'

function _clamp(n: number, min: number, max: number) {
    return Math.max(min, Math.min(max, n))
}

function _formatNumber(n: number) {
    const abs = Math.abs(n)
    if (abs >= 1000) return n.toFixed(0)
    if (abs >= 100) return n.toFixed(1)
    if (abs >= 10) return n.toFixed(2)
    return n.toFixed(3)
}

type SparklineBuilt = {
    linePath: string
    areaPath: string
    lastPoint: { x: number; y: number } | null
    min: number
    max: number
    last: number | null
}

function _buildSparkline(values: number[], width: number, height: number): SparklineBuilt {
    if (values.length === 0) return { linePath: '', areaPath: '', lastPoint: null, min: 0, max: 0, last: null }

    const margin = { left: 44, right: 14, top: 12, bottom: 26 }
    const plotW = Math.max(1, width - margin.left - margin.right)
    const plotH = Math.max(1, height - margin.top - margin.bottom)
    const bottomY = margin.top + plotH

    const rawMin = Math.min(...values)
    const rawMax = Math.max(...values)

    let min = rawMin
    let max = rawMax
    let span = max - min
    if (span === 0) {
        const pad = Math.max(1, Math.abs(max) * 0.1)
        min = min - pad
        max = max + pad
        span = max - min
    } else {
        const pad = span * 0.08
        min = min - pad
        max = max + pad
        span = max - min
    }

    const stepX = values.length === 1 ? plotW : plotW / (values.length - 1)
    const points = values.map((v, i) => {
        const x = margin.left + i * stepX
        const y = margin.top + ((max - v) / span) * plotH
        return [x, y] as const
    })

    const [x0, y0] = points[0]
    const rest = points.slice(1).map(([x, y]) => `L ${x.toFixed(2)} ${y.toFixed(2)}`).join(' ')
    const linePath = `M ${x0.toFixed(2)} ${y0.toFixed(2)} ${rest}`.trim()

    const xLast = points[points.length - 1][0]
    const yLast = points[points.length - 1][1]
    const areaPath = [
        `M ${margin.left.toFixed(2)} ${bottomY.toFixed(2)}`,
        `L ${x0.toFixed(2)} ${y0.toFixed(2)}`,
        rest,
        `L ${xLast.toFixed(2)} ${bottomY.toFixed(2)}`,
        'Z',
    ]
        .filter(Boolean)
        .join(' ')

    return {
        linePath,
        areaPath,
        lastPoint: { x: xLast, y: yLast },
        min: rawMin,
        max: rawMax,
        last: values[values.length - 1] ?? null,
    }
}

export default function TelemetryStreamModal({
    sensorId,
    isOpen,
    onClose,
    filterRunId,
    filterCaptureSessionId,
}: TelemetryStreamModalProps) {
    const [sinceTs, setSinceTs] = useState(() => new Date().toISOString())
    const [sinceId, setSinceId] = useState('0')
    const [idleTimeoutSeconds, setIdleTimeoutSeconds] = useState('30')
    const [maxPoints, setMaxPoints] = useState('200')
    const [valueMode, setValueMode] = useState<'physical' | 'raw'>('physical')
    const defaultFilterMode = useMemo<FilterMode>(() => {
        if (filterCaptureSessionId) return 'capture'
        if (filterRunId) return 'run'
        return 'all'
    }, [filterCaptureSessionId, filterRunId])
    const [filterMode, setFilterMode] = useState<FilterMode>('all')

    const [status, setStatus] = useState<StreamStatus>('idle')
    const [error, setError] = useState<string | null>(null)
    const [records, setRecords] = useState<TelemetryStreamRecord[]>([])
    const [lastId, setLastId] = useState<number>(0)
    const [lastTs, setLastTs] = useState<string>('')

    const abortRef = useRef<AbortController | null>(null)
    const runningRef = useRef(false)

    useEffect(() => {
        if (!isOpen) {
            // ensure stopped when modal closes
            abortRef.current?.abort()
            abortRef.current = null
            runningRef.current = false
            setStatus('idle')
            setError(null)
            setRecords([])
            setLastId(0)
            setLastTs('')
            setSinceTs(new Date().toISOString())
            setSinceId('0')
            setIdleTimeoutSeconds('30')
            setMaxPoints('200')
            setValueMode('physical')
            setFilterMode(defaultFilterMode)
            return
        }
        setFilterMode(defaultFilterMode)
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [isOpen, defaultFilterMode])

    const filteredRecords = useMemo(() => {
        if (filterMode === 'run' && filterRunId) {
            return records.filter((r) => r.run_id === filterRunId)
        }
        if (filterMode === 'capture' && filterCaptureSessionId) {
            return records.filter((r) => r.capture_session_id === filterCaptureSessionId)
        }
        return records
    }, [records, filterMode, filterRunId, filterCaptureSessionId])

    const plottedValues = useMemo(() => {
        const vals = filteredRecords
            .map((r) => (valueMode === 'physical' ? r.physical_value : r.raw_value))
            .map((v) => (typeof v === 'number' ? v : null))
            .filter((v): v is number => v !== null && Number.isFinite(v))
        return vals.slice(-50)
    }, [filteredRecords, valueMode])

    const SPARK_W = 640
    const SPARK_H = 170
    const spark = useMemo(() => _buildSparkline(plottedValues, SPARK_W, SPARK_H), [plottedValues])

    const stop = () => {
        abortRef.current?.abort()
        abortRef.current = null
        runningRef.current = false
        setStatus('idle')
    }

    const start = async () => {
        setError(null)

        const ts = sinceTs && Number.isFinite(Date.parse(sinceTs)) ? sinceTs : new Date().toISOString()
        const since = _clamp(Number(sinceId || '0'), 0, Number.MAX_SAFE_INTEGER)
        const idle = _clamp(Number(idleTimeoutSeconds || '30'), 1, 600)
        const max = _clamp(Number(maxPoints || '200'), 10, 2000)

        setRecords([])
        setLastId(since)
        setLastTs(ts)
        setStatus('connecting')

        const abort = new AbortController()
        abortRef.current = abort
        runningRef.current = true

        try {
            const { response: resp, debug } = await telemetryApi.stream(
                { sensor_id: sensorId, since_ts: ts, since_id: since, idle_timeout_seconds: idle }
            )
            if (!resp.ok) {
                const text = await resp.text().catch(() => '')
                const bodyText = truncateString(text || '')
                maybeEmitHttpErrorToast(
                    buildHttpDebugInfoFromFetch({
                        message: text || `Ошибка стрима: HTTP ${resp.status}`,
                        request: { method: debug.method, url: debug.url, headers: debug.headers },
                        response: {
                            status: resp.status,
                            statusText: resp.statusText,
                            headers: Object.fromEntries(resp.headers.entries()),
                            body: bodyText,
                        },
                    })
                )
                throw new Error(text || `Ошибка стрима: HTTP ${resp.status}`)
            }
            if (!resp.body) throw new Error('Stream body is empty')

            setStatus('streaming')

            const reader = resp.body.getReader()
            const decoder = new TextDecoder()
            const parser = createSSEParser((evt) => {
                if (evt.event === 'telemetry') {
                    try {
                        const parsed = JSON.parse(evt.data) as TelemetryStreamRecord
                        setLastId(parsed.id)
                        setLastTs(parsed.timestamp)
                        setRecords((prev) => {
                            const next = [...prev, parsed]
                            return next.length > max ? next.slice(next.length - max) : next
                        })
                    } catch (e: any) {
                        const msg = e?.message || 'Ошибка парсинга telemetry event'
                        setError(msg)
                        notifyError(msg)
                        setStatus('error')
                    }
                }
                if (evt.event === 'error') {
                    const msg = evt.data || 'Ошибка стрима'
                    setError(msg)
                    notifyError(msg)
                    setStatus('error')
                }
            })

            while (runningRef.current && !abort.signal.aborted) {
                const { value, done } = await reader.read()
                if (done) break
                if (value) parser.feed(decoder.decode(value, { stream: true }))
            }
        } catch (e: any) {
            if (e?.name === 'AbortError') {
                return
            }
            const debug = e?.debug as { url: string; headers: Record<string, string>; method: string } | undefined
            if (debug) {
                maybeEmitHttpErrorToast(
                    buildHttpDebugInfoFromFetch({
                        message: e?.message || 'Ошибка подключения к стриму',
                        request: { method: debug.method, url: debug.url, headers: debug.headers },
                    })
                )
            }
            setError(e?.message || 'Ошибка подключения к стриму')
            setStatus('error')
        } finally {
            runningRef.current = false
        }
    }

    const canClose = status !== 'connecting'
    const hasFilterOptions = !!filterRunId || !!filterCaptureSessionId

    return (
        <Modal
            isOpen={isOpen}
            onClose={() => {
                if (!canClose) return
                stop()
                onClose()
            }}
            title="Live telemetry (SSE)"
            disabled={!canClose}
            className="telemetry-stream-modal"
        >
            <div className="telemetry-stream">
                {IS_TEST && error && <div className="error">{error}</div>}

                <div className="form-grid">
                    <div className="form-group">
                        <label htmlFor="telemetry_stream_since_ts">since_ts</label>
                        <input
                            id="telemetry_stream_since_ts"
                            type="text"
                            value={sinceTs}
                            onChange={(e) => setSinceTs(e.target.value)}
                            disabled={status === 'connecting' || status === 'streaming'}
                        />
                    </div>

                    <div className="form-group">
                        <label htmlFor="telemetry_stream_since">since_id (tie-break)</label>
                        <input
                            id="telemetry_stream_since"
                            type="number"
                            value={sinceId}
                            onChange={(e) => setSinceId(e.target.value)}
                            disabled={status === 'connecting' || status === 'streaming'}
                            min={0}
                        />
                    </div>

                    <div className="form-group">
                        <label htmlFor="telemetry_stream_idle">idle_timeout_seconds</label>
                        <input
                            id="telemetry_stream_idle"
                            type="number"
                            value={idleTimeoutSeconds}
                            onChange={(e) => setIdleTimeoutSeconds(e.target.value)}
                            disabled={status === 'connecting' || status === 'streaming'}
                            min={1}
                            max={600}
                        />
                    </div>

                    <div className="form-group">
                        <label htmlFor="telemetry_stream_max">max points (UI)</label>
                        <input
                            id="telemetry_stream_max"
                            type="number"
                            value={maxPoints}
                            onChange={(e) => setMaxPoints(e.target.value)}
                            disabled={status === 'connecting' || status === 'streaming'}
                            min={10}
                            max={2000}
                        />
                    </div>
                    {hasFilterOptions && (
                        <div className="form-group">
                            <label htmlFor="telemetry_stream_filter">filter</label>
                            <MaterialSelect
                                id="telemetry_stream_filter"
                                value={filterMode}
                                onChange={(value) => setFilterMode(value as FilterMode)}
                                disabled={status === 'connecting'}
                            >
                                <option value="all">all events</option>
                                {filterRunId && <option value="run">this run</option>}
                                {filterCaptureSessionId && <option value="capture">active capture session</option>}
                            </MaterialSelect>
                        </div>
                    )}
                </div>

                <div className="stream-actions">
                    {status !== 'streaming' ? (
                        <button className="btn btn-primary" onClick={start} disabled={status === 'connecting'}>
                            {status === 'connecting' ? 'Подключение…' : 'Старт'}
                        </button>
                    ) : (
                        <button className="btn btn-secondary" onClick={stop}>
                            Стоп
                        </button>
                    )}
                    <div className="stream-meta">
                        <span className="mono">sensor_id: {sensorId}</span>
                        {filterRunId && <span className="mono">run_id: {filterRunId}</span>}
                        {filterCaptureSessionId && <span className="mono">cs_id: {filterCaptureSessionId}</span>}
                        <span className="mono">last_id: {lastId}</span>
                        {lastTs && <span className="mono">last_ts: {lastTs}</span>}
                        <span>events: {filteredRecords.length}{filterMode !== 'all' ? ` / ${records.length}` : ''}</span>
                    </div>
                </div>

                <div className="sparkline-card">
                    <div className="sparkline-header">
                        <strong>График (последние 50)</strong>
                        <div className="sparkline-stats">
                            {spark.last !== null ? (
                                <>
                                    <span className="mono">
                                        last={_formatNumber(spark.last)}{' '}
                                        <span className="dim">
                                            (min={_formatNumber(spark.min)} max={_formatNumber(spark.max)})
                                        </span>
                                    </span>
                                </>
                            ) : (
                                <span className="dim">нет данных</span>
                            )}
                        </div>
                        <div className="mode-toggle">
                            <label>
                                <input
                                    type="radio"
                                    name="telemetry_value_mode"
                                    checked={valueMode === 'physical'}
                                    onChange={() => setValueMode('physical')}
                                />
                                physical
                            </label>
                            <label>
                                <input
                                    type="radio"
                                    name="telemetry_value_mode"
                                    checked={valueMode === 'raw'}
                                    onChange={() => setValueMode('raw')}
                                />
                                raw
                            </label>
                        </div>
                    </div>
                    <svg
                        className="sparkline-svg"
                        width="100%"
                        height="170"
                        viewBox={`0 0 ${SPARK_W} ${SPARK_H}`}
                        role="img"
                        aria-label="telemetry sparkline"
                        preserveAspectRatio="none"
                    >
                        <title>Последние 50 значений ({valueMode})</title>
                        <rect x="0" y="0" width={SPARK_W} height={SPARK_H} className="sparkline-bg" />

                        {/* grid */}
                        <g className="sparkline-grid">
                            <line x1="44" y1="12" x2="44" y2="144" />
                            <line x1="44" y1="144" x2="626" y2="144" />
                            <line x1="44" y1="12" x2="626" y2="12" />
                            <line x1="44" y1={(12 + (144 - 12) * 0.33).toFixed(2)} x2="626" y2={(12 + (144 - 12) * 0.33).toFixed(2)} />
                            <line x1="44" y1={(12 + (144 - 12) * 0.66).toFixed(2)} x2="626" y2={(12 + (144 - 12) * 0.66).toFixed(2)} />
                        </g>

                        {/* y labels */}
                        {plottedValues.length > 0 && (
                            <g className="sparkline-labels">
                                <text x="8" y="18">
                                    {_formatNumber(spark.max)}
                                </text>
                                <text x="8" y="148">
                                    {_formatNumber(spark.min)}
                                </text>
                                <text x="44" y="166">
                                    старые → новые
                                </text>
                            </g>
                        )}

                        {plottedValues.length === 0 ? (
                            <g className="sparkline-empty">
                                <text x="50%" y="50%" dominantBaseline="middle" textAnchor="middle">
                                    Нет данных — нажмите «Старт»
                                </text>
                            </g>
                        ) : (
                            <>
                                <path d={spark.areaPath} className="sparkline-area" />
                                <path d={spark.linePath} className="sparkline-path" />
                                {spark.lastPoint && (
                                    <circle
                                        className="sparkline-last"
                                        cx={spark.lastPoint.x}
                                        cy={spark.lastPoint.y}
                                        r="3.5"
                                    />
                                )}
                            </>
                        )}
                    </svg>
                </div>

                <div className="records">
                    <div className="records-header">
                        <strong>Последние события</strong>
                    </div>
                    {filteredRecords.length === 0 ? (
                        <div className="empty">Событий пока нет</div>
                    ) : (
                        <div className="records-list">
                            {filteredRecords
                                .slice()
                                .reverse()
                                .slice(0, 25)
                                .map((r) => (
                                    <div key={r.id} className="record-row">
                                        <span className="mono">#{r.id}</span>
                                        <span className="mono">{new Date(r.timestamp).toLocaleString()}</span>
                                        <span className="mono">
                                            raw={r.raw_value}
                                            {typeof r.physical_value === 'number' ? ` phys=${r.physical_value}` : ''}
                                        </span>
                                        {r.run_id && <span className="mono">run={r.run_id}</span>}
                                        {r.capture_session_id && <span className="mono">cs={r.capture_session_id}</span>}
                                    </div>
                                ))}
                        </div>
                    )}
                </div>
            </div>
        </Modal>
    )
}

