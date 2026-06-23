import { format } from 'date-fns'
import type { CaptureSession, Experiment } from '../../types'
import { StatusBadge, Loading, EmptyState, InfoRow, captureSessionStatusMap } from '../../components/common'
import AuditLog from '../../components/AuditLog'

interface SessionMutation<TVar = void> {
  mutate: (variables: TVar) => void
  isPending: boolean
}

interface RunCaptureSessionsProps {
  runId: string
  sessions: CaptureSession[]
  activeSession: CaptureSession | undefined
  sessionsLoading: boolean
  canManageSessions: boolean
  experiment: Experiment | undefined
  createSessionMutation: SessionMutation<string | undefined>
  stopSessionMutation: SessionMutation<string>
  deleteSessionMutation: SessionMutation<string>
  setExportTarget: (
    target: { mode: 'session'; sessionId: string; sessionOrdinal: number } | { mode: 'run' }
  ) => void
  formatSessionDuration: (startedAt?: string | null, stoppedAt?: string | null) => string
}

export default function RunCaptureSessions({
  runId,
  sessions,
  activeSession,
  sessionsLoading,
  canManageSessions,
  experiment,
  createSessionMutation,
  stopSessionMutation,
  deleteSessionMutation,
  setExportTarget,
  formatSessionDuration,
}: RunCaptureSessionsProps) {
  return (
    <section className="capture-sessions-section card detail-card">
      <div className="detail-section-header">
        <div className="detail-section-header__copy">
          <span className="detail-card__eyebrow">Capture Sessions</span>
          <h3 className="detail-card__title">Сессии отсчёта</h3>
          <p>История активных и завершенных интервалов сбора с аудитом на уровне каждой сессии.</p>
        </div>
        {sessions.length > 0 && (
          <button className="btn btn-secondary btn-sm" onClick={() => setExportTarget({ mode: 'run' })}>
            Экспорт телеметрии…
          </button>
        )}
      </div>

      {sessionsLoading ? (
        <Loading message="Загрузка сессий..." />
      ) : sessions.length === 0 ? (
        <EmptyState message="Сессии отсчёта отсутствуют">
          {canManageSessions && !activeSession && experiment && (
            <button
              className="btn btn-primary"
              onClick={() => {
                const notes = prompt('Заметки (опционально):')
                createSessionMutation.mutate(notes || undefined)
              }}
              disabled={createSessionMutation.isPending}
            >
              Создать сессию
            </button>
          )}
        </EmptyState>
      ) : (
        <div className="sessions-list">
          {activeSession && (
            <div className="session-card active">
              <div className="session-header">
                <div>
                  <h4>Активная сессия #{activeSession.ordinal_number}</h4>
                  <StatusBadge status={activeSession.status} statusMap={captureSessionStatusMap} />
                </div>
                <button
                  className="btn btn-danger btn-sm"
                  onClick={() => {
                    if (confirm('Остановить отсчёт?')) {
                      stopSessionMutation.mutate(activeSession.id)
                    }
                  }}
                  disabled={stopSessionMutation.isPending}
                >
                  Стоп
                </button>
              </div>
              {activeSession.started_at && (
                <div className="session-info">
                  <InfoRow label="Начало" value={format(new Date(activeSession.started_at), 'dd MMM yyyy HH:mm:ss')} />
                  <InfoRow
                    label="Длительность"
                    value={formatSessionDuration(activeSession.started_at, activeSession.stopped_at)}
                  />
                  {activeSession.notes && <InfoRow label="Заметки" value={activeSession.notes} />}
                </div>
              )}
              <AuditLog
                runId={runId}
                captureSessionId={activeSession.id}
                title={`События сессии #${activeSession.ordinal_number}`}
              />
            </div>
          )}

          {sessions
            .filter(
              (s: CaptureSession) => s.status !== 'running' && s.status !== 'backfilling'
            )
            .sort((a: CaptureSession, b: CaptureSession) => b.ordinal_number - a.ordinal_number)
            .map((session: CaptureSession) => (
              <div key={session.id} className="session-card">
                <div className="session-header">
                  <div>
                    <h4>Сессия #{session.ordinal_number}</h4>
                    <StatusBadge status={session.status} statusMap={captureSessionStatusMap} />
                  </div>
                  <div className="session-actions">
                    <button
                      className="btn btn-secondary btn-sm"
                      onClick={() =>
                        setExportTarget({
                          mode: 'session',
                          sessionId: session.id,
                          sessionOrdinal: session.ordinal_number,
                        })
                      }
                    >
                      Экспорт телеметрии…
                    </button>
                    {session.status !== 'archived' && (
                      <button
                        className="btn btn-secondary btn-sm"
                        onClick={() => {
                          if (confirm('Удалить сессию?')) {
                            deleteSessionMutation.mutate(session.id)
                          }
                        }}
                        disabled={deleteSessionMutation.isPending}
                      >
                        Удалить
                      </button>
                    )}
                  </div>
                </div>
                <div className="session-info">
                  {session.started_at && (
                    <InfoRow label="Начало" value={format(new Date(session.started_at), 'dd MMM yyyy HH:mm:ss')} />
                  )}
                  {session.stopped_at && (
                    <InfoRow label="Остановка" value={format(new Date(session.stopped_at), 'dd MMM yyyy HH:mm:ss')} />
                  )}
                  {session.started_at && (
                    <InfoRow
                      label="Длительность"
                      value={formatSessionDuration(session.started_at, session.stopped_at)}
                    />
                  )}
                  {session.notes && <InfoRow label="Заметки" value={session.notes} />}
                </div>
                <AuditLog
                  runId={runId}
                  captureSessionId={session.id}
                  title={`События сессии #${session.ordinal_number}`}
                />
              </div>
            ))}
        </div>
      )}
    </section>
  )
}
