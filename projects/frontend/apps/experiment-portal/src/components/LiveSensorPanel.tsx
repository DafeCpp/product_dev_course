import { useMemo } from 'react'
import { useQuery } from '@tanstack/react-query'
import { sensorsApi } from '../api/client'
import type { Sensor } from '../types/index'
import './LiveSensorPanel.scss'

interface LiveSensorPanelProps {
    sensors: Sensor[]
    projectId: string
    recentValues?: Record<string, number[]>
}

const STATUS_DOT: Record<string, string> = {
    online: '#16a34a',
    delayed: '#d97706',
    offline: '#6b7280',
}

const STATUS_LABEL: Record<string, string> = {
    online: 'Online',
    delayed: 'Delayed',
    offline: 'Offline',
}

function formatHeartbeat(heartbeat?: string | null): string {
    if (!heartbeat) return 'Нет данных'
    const diffMs = Date.now() - new Date(heartbeat).getTime()
    if (diffMs < 0) return 'Только что'
    const seconds = Math.floor(diffMs / 1000)
    if (seconds < 5) return 'Только что'
    if (seconds < 60) return `${seconds}с назад`
    const minutes = Math.floor(seconds / 60)
    if (minutes < 60) return `${minutes}м назад`
    const hours = Math.floor(minutes / 60)
    return `${hours}ч назад`
}

function formatLatency(heartbeat?: string | null): { text: string; level: 'green' | 'amber' | 'gray' } {
    if (!heartbeat) return { text: '—', level: 'gray' }
    const diffMs = Date.now() - new Date(heartbeat).getTime()
    if (diffMs < 0) return { text: '0 ms', level: 'green' }

    const seconds = diffMs / 1000
    if (seconds > 300) return { text: '—', level: 'gray' }

    let level: 'green' | 'amber' | 'gray'
    if (seconds < 5) level = 'green'
    else if (seconds <= 30) level = 'amber'
    else level = 'gray'

    let text: string
    if (diffMs < 100) {
        text = `${diffMs} ms`
    } else {
        text = `${seconds.toFixed(1)}с`
    }

    return { text, level }
}

function HeartbeatSparkline({ sensorId }: { sensorId: string }) {
    const { data } = useQuery({
        queryKey: ['heartbeat-history', sensorId],
        queryFn: () => sensorsApi.getHeartbeatHistory(sensorId, 60),
        refetchInterval: 30_000,
        staleTime: 15_000,
    })

    const timestamps = data?.timestamps || []

    const points = useMemo(() => {
        if (timestamps.length < 2) return null

        // Build minute-level density: count heartbeats per minute bucket
        const now = Date.now()
        const bucketCount = 30 // 30 buckets over 60 minutes = 2 min each
        const windowMs = 60 * 60 * 1000
        const bucketMs = windowMs / bucketCount
        const counts = new Array(bucketCount).fill(0)

        for (const ts of timestamps) {
            const age = now - new Date(ts).getTime()
            const idx = Math.floor((windowMs - age) / bucketMs)
            if (idx >= 0 && idx < bucketCount) counts[idx]++
        }

        const max = Math.max(...counts, 1)
        const width = 100
        const height = 20
        return counts
            .map((c, i) => {
                const x = (i / (bucketCount - 1)) * width
                const y = height - (c / max) * height
                return `${x.toFixed(1)},${y.toFixed(1)}`
            })
            .join(' ')
    }, [timestamps])

    if (!points) {
        return <div className="lsp__sparkline lsp__sparkline--empty" />
    }

    return (
        <svg className="lsp__sparkline" viewBox="0 0 100 20" preserveAspectRatio="none">
            <polyline
                points={points}
                fill="none"
                stroke="currentColor"
                strokeWidth="1.5"
                strokeLinecap="round"
                strokeLinejoin="round"
            />
        </svg>
    )
}

function ValueSparkline({ values, displayUnit }: { values: number[]; displayUnit: string }) {
    const points = useMemo(() => {
        if (values.length < 2) return null
        const width = 100
        const height = 16
        const min = Math.min(...values)
        const max = Math.max(...values)
        const range = max - min || 1
        return values
            .map((v, i) => {
                const x = (i / (values.length - 1)) * width
                const y = height - ((v - min) / range) * height
                return `${x.toFixed(1)},${y.toFixed(1)}`
            })
            .join(' ')
    }, [values])

    const lastValue = values[values.length - 1]
    const minVal = Math.min(...values)
    const maxVal = Math.max(...values)

    if (!points) return null

    return (
        <div className="lsp__value-sparkline-wrap">
            <svg
                className="lsp__value-sparkline"
                viewBox="0 0 100 16"
                preserveAspectRatio="none"
            >
                <polyline
                    points={points}
                    fill="none"
                    stroke="#9ca3af"
                    strokeWidth="1.5"
                    strokeLinecap="round"
                    strokeLinejoin="round"
                />
            </svg>
            <span className="lsp__value-sparkline-last">
                {lastValue.toFixed(2)} {displayUnit}
            </span>
            <span className="lsp__value-sparkline-range">
                {minVal.toFixed(1)} – {maxVal.toFixed(1)}
            </span>
        </div>
    )
}

function ErrorBadge({ sensorId }: { sensorId: string }) {
    const since = useMemo(() => {
        const d = new Date()
        d.setHours(d.getHours() - 24)
        return d.toISOString()
    }, [])

    const { data } = useQuery({
        queryKey: ['sensor-error-log-count', sensorId, since],
        queryFn: () => sensorsApi.getErrorLog(sensorId, { limit: 100 }),
        refetchInterval: 60_000,
        staleTime: 30_000,
    })

    const count = data?.entries?.length ?? 0
    if (count === 0) return null

    return (
        <span className="lsp__error-badge" title={`${count} ошибок за 24ч`}>
            {count > 99 ? '99+' : count}
        </span>
    )
}

export default function LiveSensorPanel({ sensors, projectId, recentValues }: LiveSensorPanelProps) {
    const { data: statusSummary } = useQuery({
        queryKey: ['sensors', 'status-summary', projectId],
        queryFn: () => sensorsApi.getStatusSummary(projectId),
        enabled: !!projectId,
        refetchInterval: 15_000,
        staleTime: 10_000,
    })

    // Re-poll sensors for fresh connection_status
    const { data: freshSensors } = useQuery({
        queryKey: ['sensors-live-status', projectId],
        queryFn: () => sensorsApi.list({ project_id: projectId, limit: 100 }),
        enabled: !!projectId,
        refetchInterval: 15_000,
        staleTime: 10_000,
    })

    const sensorStatus = useMemo(() => {
        const map = new Map<string, Sensor>()
        const list = freshSensors?.sensors || sensors
        for (const s of list) map.set(s.id, s)
        return map
    }, [freshSensors, sensors])

    const sortedSensors = useMemo(() => {
        const order: Record<string, number> = { online: 0, delayed: 1, offline: 2 }
        return [...sensors].sort((a, b) => {
            const sa = sensorStatus.get(a.id)
            const sb = sensorStatus.get(b.id)
            const oa = order[sa?.connection_status || 'offline'] ?? 2
            const ob = order[sb?.connection_status || 'offline'] ?? 2
            return oa - ob
        })
    }, [sensors, sensorStatus])

    if (!sensors.length) return null

    return (
        <aside className="lsp">
            <div className="lsp__header">
                <span className="lsp__title">Датчики</span>
                {statusSummary && (
                    <div className="lsp__summary">
                        <span className="lsp__summary-chip lsp__summary-chip--online">
                            {statusSummary.online}
                        </span>
                        <span className="lsp__summary-chip lsp__summary-chip--delayed">
                            {statusSummary.delayed}
                        </span>
                        <span className="lsp__summary-chip lsp__summary-chip--offline">
                            {statusSummary.offline}
                        </span>
                    </div>
                )}
            </div>

            <div className="lsp__list">
                {sortedSensors.map((sensor) => {
                    const fresh = sensorStatus.get(sensor.id)
                    const status = fresh?.connection_status || 'offline'
                    const dotColor = STATUS_DOT[status] || STATUS_DOT.offline
                    const heartbeat = fresh?.last_heartbeat ?? sensor.last_heartbeat
                    const latency = formatLatency(heartbeat)
                    const sensorValues = recentValues?.[sensor.id]
                    const lastVal = sensorValues && sensorValues.length > 0
                        ? sensorValues[sensorValues.length - 1]
                        : undefined

                    return (
                        <div key={sensor.id} className="lsp__card">
                            <div className="lsp__card-top">
                                <span
                                    className={`lsp__dot lsp__dot--${status}`}
                                    style={{ backgroundColor: dotColor }}
                                    title={STATUS_LABEL[status]}
                                />
                                <span className="lsp__name" title={sensor.name}>
                                    {sensor.name}
                                </span>
                                <span className="lsp__type">{sensor.type}</span>
                                <ErrorBadge sensorId={sensor.id} />
                            </div>
                            <div className="lsp__card-mid">
                                <HeartbeatSparkline sensorId={sensor.id} />
                                {sensorValues && sensorValues.length >= 2 && (
                                    <ValueSparkline
                                        values={sensorValues}
                                        displayUnit={sensor.display_unit}
                                    />
                                )}
                            </div>
                            <div className="lsp__card-bot">
                                {lastVal !== undefined && (
                                    <span className="lsp__last-value">
                                        {lastVal.toFixed(2)} {sensor.display_unit}
                                    </span>
                                )}
                                <span className="lsp__heartbeat">
                                    {formatHeartbeat(heartbeat)}
                                </span>
                                <span className={`lsp__latency lsp__latency--${latency.level}`}>
                                    {latency.text}
                                </span>
                                <span className={`lsp__status lsp__status--${status}`}>
                                    {STATUS_LABEL[status]}
                                </span>
                            </div>
                        </div>
                    )
                })}
            </div>
        </aside>
    )
}
