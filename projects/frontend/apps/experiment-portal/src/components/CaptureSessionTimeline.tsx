import { useMemo } from 'react'
import type { CaptureSession } from '../types'
import './CaptureSessionTimeline.scss'

const STATUS_COLORS: Record<string, string> = {
    running: '#16a34a',
    succeeded: '#2563eb',
    failed: '#dc2626',
    backfilling: '#f97316',
    draft: '#94a3b8',
    archived: '#64748b',
}

const STATUS_LABELS: Record<string, string> = {
    running: 'Выполняется',
    succeeded: 'Завершён',
    failed: 'Ошибка',
    backfilling: 'Догрузка',
    draft: 'Черновик',
    archived: 'Архив',
}

interface CaptureSessionTimelineProps {
    sessions: CaptureSession[]
    /** Current visible time range from Plotly [start, end] as ISO strings */
    timeRange?: [string, string] | null
    onSessionClick?: (sessionId: string) => void
}

function formatDuration(startedAt?: string | null, stoppedAt?: string | null): string {
    if (!startedAt) return '—'
    const start = new Date(startedAt).getTime()
    const end = stoppedAt ? new Date(stoppedAt).getTime() : Date.now()
    const diffMs = end - start
    if (diffMs < 0) return '—'
    const seconds = Math.floor(diffMs / 1000)
    if (seconds < 60) return `${seconds}с`
    const minutes = Math.floor(seconds / 60)
    if (minutes < 60) return `${minutes}м ${seconds % 60}с`
    const hours = Math.floor(minutes / 60)
    return `${hours}ч ${minutes % 60}м`
}

export default function CaptureSessionTimeline({
    sessions,
    timeRange,
    onSessionClick,
}: CaptureSessionTimelineProps) {
    const visibleSessions = useMemo(() => {
        if (!sessions.length) return []

        const sorted = sessions
            .filter((s) => s.started_at)
            .sort(
                (a, b) =>
                    new Date(a.started_at!).getTime() - new Date(b.started_at!).getTime()
            )

        if (!timeRange) return sorted

        const [rangeStart, rangeEnd] = timeRange
        const rs = new Date(rangeStart).getTime()
        const re = new Date(rangeEnd).getTime()

        return sorted.filter((s) => {
            const ss = new Date(s.started_at!).getTime()
            const se = s.stopped_at ? new Date(s.stopped_at).getTime() : Date.now()
            return se >= rs && ss <= re
        })
    }, [sessions, timeRange])

    const { rangeStartMs, rangeEndMs } = useMemo(() => {
        if (timeRange) {
            return {
                rangeStartMs: new Date(timeRange[0]).getTime(),
                rangeEndMs: new Date(timeRange[1]).getTime(),
            }
        }
        if (!visibleSessions.length) {
            return { rangeStartMs: 0, rangeEndMs: 1 }
        }
        const starts = visibleSessions.map((s) => new Date(s.started_at!).getTime())
        const ends = visibleSessions.map((s) =>
            s.stopped_at ? new Date(s.stopped_at).getTime() : Date.now()
        )
        return {
            rangeStartMs: Math.min(...starts),
            rangeEndMs: Math.max(...ends),
        }
    }, [timeRange, visibleSessions])

    const totalMs = Math.max(1, rangeEndMs - rangeStartMs)

    if (!visibleSessions.length) return null

    return (
        <div className="cs-timeline">
            <div className="cs-timeline__track">
                {visibleSessions.map((session) => {
                    const startMs = new Date(session.started_at!).getTime()
                    const endMs = session.stopped_at
                        ? new Date(session.stopped_at).getTime()
                        : Date.now()

                    const leftPct = Math.max(0, ((startMs - rangeStartMs) / totalMs) * 100)
                    const widthPct = Math.max(
                        0.5,
                        Math.min(100 - leftPct, ((endMs - startMs) / totalMs) * 100)
                    )

                    const color = STATUS_COLORS[session.status] || '#94a3b8'
                    const label = STATUS_LABELS[session.status] || session.status

                    return (
                        <div
                            key={session.id}
                            className="cs-timeline__bar"
                            style={{
                                left: `${leftPct}%`,
                                width: `${widthPct}%`,
                                backgroundColor: color,
                            }}
                            title={`#${session.ordinal_number} · ${label} · ${formatDuration(session.started_at, session.stopped_at)}`}
                            onClick={() => onSessionClick?.(session.id)}
                            role="button"
                            tabIndex={0}
                            onKeyDown={(e) => {
                                if (e.key === 'Enter' || e.key === ' ') {
                                    e.preventDefault()
                                    onSessionClick?.(session.id)
                                }
                            }}
                        >
                            {widthPct > 8 && (
                                <span className="cs-timeline__label">
                                    #{session.ordinal_number}
                                </span>
                            )}
                        </div>
                    )
                })}
            </div>
            <div className="cs-timeline__legend">
                {(['running', 'succeeded', 'failed', 'backfilling'] as const).map((status) => (
                    <span key={status} className="cs-timeline__legend-item">
                        <span
                            className="cs-timeline__legend-dot"
                            style={{ backgroundColor: STATUS_COLORS[status] }}
                        />
                        {STATUS_LABELS[status]}
                    </span>
                ))}
            </div>
        </div>
    )
}
