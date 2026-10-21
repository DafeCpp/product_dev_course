import { memo, useEffect, useMemo, useRef, useSyncExternalStore } from 'react'
import Plotly from 'plotly.js-dist-min'
import type { Sensor } from '../../types'
import type { TelemetryRecordsStore } from './telemetryRecordsStore'

/** Internal safety cap — prevents chart from rendering too many DOM elements. */
export const MAX_POINTS_CAP = 5000

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

function parseTimestampMs(value: string) {
    const parsed = Date.parse(value)
    return Number.isFinite(parsed) ? parsed : null
}

type LiveTelemetryChartProps = {
    store: TelemetryRecordsStore
    sensors: Sensor[]
    valueMode: 'physical' | 'raw'
    timeWindowSeconds: number
    useLatestAnchor: boolean
    streaming: boolean
}

/**
 * The high-frequency part of a telemetry panel: subscribes to the panel's
 * records store and redraws the Plotly chart and legend. Memoized, so the
 * panel's settings and header don't re-render on every stream update, and
 * the chart doesn't re-render on panel changes that don't touch its props.
 */
function LiveTelemetryChart({
    store,
    sensors,
    valueMode,
    timeWindowSeconds,
    useLatestAnchor,
    streaming,
}: LiveTelemetryChartProps) {
    const recordsBySensor = useSyncExternalStore(store.subscribe, store.getSnapshot)
    const rootRef = useRef<HTMLDivElement | null>(null)
    const plotRef = useRef<HTMLDivElement | null>(null)
    const scrollAnimRef = useRef<number | null>(null)

    const seriesData = useMemo(() => {
        const allRecords = sensors.flatMap((sensor) => recordsBySensor[sensor.id] || [])
        const latestTimestampMs = allRecords.reduce((latest, record) => {
            const ts = parseTimestampMs(record.timestamp)
            if (ts === null) return latest
            return ts > latest ? ts : latest
        }, Number.NEGATIVE_INFINITY)
        const nowMs = Date.now()
        const windowMs = timeWindowSeconds * 1000
        const anchorMs = useLatestAnchor ? latestTimestampMs : nowMs
        const hasWindowAnchor = Number.isFinite(anchorMs) && anchorMs > 0

        return sensors.map((sensor) => {
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
    }, [sensors, recordsBySensor, valueMode, timeWindowSeconds, useLatestAnchor])

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
                ...(streaming
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
    }, [streaming, timeWindowSeconds])

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
        const root = rootRef.current
        if (!element || typeof ResizeObserver === 'undefined') return
        const observer = new ResizeObserver(() => {
            Plotly.Plots.resize(element)
        })
        observer.observe(element)
        if (root) observer.observe(root)
        return () => observer.disconnect()
    }, [])

    // ---- Smooth x-axis scroll during live streaming ----
    useEffect(() => {
        if (!streaming) {
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
    }, [streaming, timeWindowSeconds, hasData])

    useEffect(() => {
        return () => {
            const element = plotRef.current
            if (element) Plotly.purge(element)
            if (scrollAnimRef.current !== null) cancelAnimationFrame(scrollAnimRef.current)
        }
    }, [])

    return (
        <div ref={rootRef} className="telemetry-panel__chart">
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
    )
}

export default memo(LiveTelemetryChart)
