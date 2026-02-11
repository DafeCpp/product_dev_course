import { useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { runEventsApi, captureSessionEventsApi } from '../api/client'
import { format } from 'date-fns'
import type { RunEvent, CaptureSessionEvent } from '../types'
import './AuditLog.scss'

const PAGE_SIZE = 20

/** Determine timeline dot variant based on event_type string. */
function dotVariant(eventType: string): string {
  if (eventType.includes('created') || eventType.includes('started')) return 'create'
  if (eventType.includes('status') || eventType.includes('stopped') || eventType.includes('finished')) return 'status'
  if (eventType.includes('deleted') || eventType.includes('archived')) return 'delete'
  if (eventType.includes('tags')) return 'tag'
  if (eventType.includes('error') || eventType.includes('failed')) return 'error'
  return ''
}

/** Human-friendly event label (keep original as fallback). */
function eventLabel(eventType: string): string {
  const map: Record<string, string> = {
    'run.started': 'Run запущен',
    'run.finished': 'Run завершён',
    'run.status_changed': 'Статус изменён',
    'run.archived': 'Run архивирован',
    'run.tags_updated': 'Теги обновлены',
    'capture_session.created': 'Сессия создана',
    'capture_session.stopped': 'Сессия остановлена',
  }
  return map[eventType] || eventType
}

interface AuditLogProps {
  /** If provided — show run-level events. */
  runId?: string
  /** If provided together with runId — show capture-session-level events. */
  captureSessionId?: string
  /** Label shown above the log. */
  title?: string
  /** Start collapsed. Default: true */
  defaultCollapsed?: boolean
}

type AuditEvent = (RunEvent | CaptureSessionEvent) & { _kind: 'run' | 'session' }

export default function AuditLog({
  runId,
  captureSessionId,
  title = 'История событий',
  defaultCollapsed = true,
}: AuditLogProps) {
  const [collapsed, setCollapsed] = useState(defaultCollapsed)
  const [page, setPage] = useState(0)

  const isSessionMode = !!(runId && captureSessionId)

  const {
    data: runEventsData,
    isLoading: runEventsLoading,
    error: runEventsError,
  } = useQuery({
    queryKey: ['run-events', runId, page],
    queryFn: () =>
      runEventsApi.list(runId!, {
        page: page + 1,
        page_size: PAGE_SIZE,
      }),
    enabled: !!runId && !isSessionMode && !collapsed,
  })

  const {
    data: sessionEventsData,
    isLoading: sessionEventsLoading,
    error: sessionEventsError,
  } = useQuery({
    queryKey: ['capture-session-events', runId, captureSessionId, page],
    queryFn: () =>
      captureSessionEventsApi.list(runId!, captureSessionId!, {
        page: page + 1,
        page_size: PAGE_SIZE,
      }),
    enabled: isSessionMode && !collapsed,
  })

  const isLoading = isSessionMode ? sessionEventsLoading : runEventsLoading
  const error = isSessionMode ? sessionEventsError : runEventsError
  const rawEvents = isSessionMode
    ? (sessionEventsData?.events || [])
    : (runEventsData?.events || [])
  const total = isSessionMode
    ? (sessionEventsData?.total ?? 0)
    : (runEventsData?.total ?? 0)

  const events: AuditEvent[] = rawEvents.map((e: any) => ({
    ...e,
    _kind: isSessionMode ? 'session' : 'run',
  }))

  const totalPages = Math.max(1, Math.ceil(total / PAGE_SIZE))

  return (
    <div className="audit-log">
      <button
        className="audit-log__toggle"
        onClick={() => setCollapsed((v) => !v)}
        type="button"
      >
        <span className={`chevron ${collapsed ? '' : 'open'}`}>&#9654;</span>
        {title} {total > 0 && <span>({total})</span>}
      </button>

      {!collapsed && (
        <div className="audit-log__content">
          {isLoading && (
            <div className="audit-log__loading">Загрузка событий...</div>
          )}

          {error && (
            <div className="audit-log__error">
              Не удалось загрузить события
            </div>
          )}

          {!isLoading && !error && events.length === 0 && (
            <div className="audit-log__empty">Событий пока нет</div>
          )}

          {!isLoading && !error && events.length > 0 && (
            <>
              <ul className="audit-log__timeline">
                {events.map((evt) => (
                  <li key={evt.id} className="audit-log__item">
                    <span
                      className={`audit-log__dot ${dotVariant(evt.event_type) ? `audit-log__dot--${dotVariant(evt.event_type)}` : ''}`}
                    />
                    <div className="audit-log__body">
                      <div className="audit-log__header">
                        <span className="audit-log__event-type">
                          {eventLabel(evt.event_type)}{' '}
                          <span className="event-badge">{evt.event_type}</span>
                        </span>
                        <span className="audit-log__time">
                          {format(new Date(evt.created_at), 'dd MMM yyyy HH:mm:ss')}
                        </span>
                      </div>
                      <div className="audit-log__actor">
                        actor: <span className="mono">{evt.actor_id}</span>
                        <span className="role-badge">{evt.actor_role}</span>
                      </div>
                      {evt.payload && Object.keys(evt.payload).length > 0 && (
                        <pre className="audit-log__payload">
                          {JSON.stringify(evt.payload, null, 2)}
                        </pre>
                      )}
                    </div>
                  </li>
                ))}
              </ul>

              {totalPages > 1 && (
                <div className="audit-log__pagination">
                  <button
                    disabled={page === 0}
                    onClick={() => setPage((p) => Math.max(0, p - 1))}
                  >
                    ← Назад
                  </button>
                  <span>
                    {page + 1} / {totalPages}
                  </span>
                  <button
                    disabled={page + 1 >= totalPages}
                    onClick={() => setPage((p) => p + 1)}
                  >
                    Вперёд →
                  </button>
                </div>
              )}
            </>
          )}
        </div>
      )}
    </div>
  )
}
