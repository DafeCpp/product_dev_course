import { useMemo } from 'react'
import { useQuery } from '@tanstack/react-query'
import { sensorsApi } from '../api/client'
import type { Sensor } from '../types'
import './LiveSensorPanel.scss'

interface LiveSensorPanelProps {
    sensors: Sensor[]
    projectId: string
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

export default function LiveSensorPanel({ sensors, projectId }: LiveSensorPanelProps) {
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

                    return (
                        <div key={sensor.id} className="lsp__card">
                            <div className="lsp__card-top">
                                <span
                                    className="lsp__dot"
                                    style={{ backgroundColor: dotColor }}
                                    title={STATUS_LABEL[status]}
                                />
                                <span className="lsp__name" title={sensor.name}>
                                    {sensor.name}
                                </span>
                                <span className="lsp__type">{sensor.type}</span>
                            </div>
                            <div className="lsp__card-mid">
                                <HeartbeatSparkline sensorId={sensor.id} />
                            </div>
                            <div className="lsp__card-bot">
                                <span className="lsp__heartbeat">
                                    {formatHeartbeat(heartbeat)}
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
