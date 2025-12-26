import { useParams, Link } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { runsApi, experimentsApi, captureSessionsApi } from '../api/client'
import { format } from 'date-fns'
import type { CaptureSession } from '../types'
import './RunDetail.css'

function RunDetail() {
  const { id } = useParams<{ id: string }>()
  const queryClient = useQueryClient()

  const { data: run, isLoading, error } = useQuery({
    queryKey: ['run', id],
    queryFn: () => runsApi.get(id!),
    enabled: !!id,
  })

  // Получаем эксперимент для project_id
  const { data: experiment } = useQuery({
    queryKey: ['experiment', run?.experiment_id],
    queryFn: () => experimentsApi.get(run!.experiment_id),
    enabled: !!run?.experiment_id,
  })

  // Получаем список capture sessions
  const { data: sessionsData, isLoading: sessionsLoading } = useQuery({
    queryKey: ['capture-sessions', id],
    queryFn: () => captureSessionsApi.list(id!),
    enabled: !!id,
  })

  const sessions = sessionsData?.capture_sessions || []
  const activeSession = sessions.find((s: CaptureSession) => s.status === 'running')

  const completeMutation = useMutation({
    mutationFn: () => runsApi.complete(id!),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['run', id] })
      queryClient.invalidateQueries({ queryKey: ['runs'] })
    },
  })

  const failMutation = useMutation({
    mutationFn: (reason?: string) => runsApi.fail(id!, reason),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['run', id] })
      queryClient.invalidateQueries({ queryKey: ['runs'] })
    },
  })

  // Создание capture session
  const createSessionMutation = useMutation({
    mutationFn: (notes?: string) => {
      if (!experiment) throw new Error('Experiment not loaded')
      const nextOrdinal = sessions.length > 0
        ? Math.max(...sessions.map((s: CaptureSession) => s.ordinal_number)) + 1
        : 1
      return captureSessionsApi.create(id!, {
        project_id: experiment.project_id,
        run_id: id!,
        ordinal_number: nextOrdinal,
        notes: notes || undefined,
      }, { project_id: experiment.project_id })
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['capture-sessions', id] })
    },
  })

  // Остановка capture session
  const stopSessionMutation = useMutation({
    mutationFn: (sessionId: string) => captureSessionsApi.stop(id!, sessionId),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['capture-sessions', id] })
    },
  })

  // Удаление capture session
  const deleteSessionMutation = useMutation({
    mutationFn: (sessionId: string) => captureSessionsApi.delete(id!, sessionId),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['capture-sessions', id] })
    },
  })

  const formatDuration = (seconds?: number) => {
    if (!seconds) return '-'
    const hours = Math.floor(seconds / 3600)
    const minutes = Math.floor((seconds % 3600) / 60)
    const secs = seconds % 60
    if (hours > 0) {
      return `${hours}ч ${minutes}м ${secs}с`
    }
    if (minutes > 0) {
      return `${minutes}м ${secs}с`
    }
    return `${secs}с`
  }

  const getStatusBadge = (status: string) => {
    const badges: Record<string, string> = {
      created: 'badge-secondary',
      running: 'badge-info',
      completed: 'badge-success',
      failed: 'badge-danger',
    }
    return badges[status] || 'badge-secondary'
  }

  const getStatusText = (status: string) => {
    const texts: Record<string, string> = {
      created: 'Создан',
      running: 'Выполняется',
      completed: 'Завершен',
      failed: 'Ошибка',
    }
    return texts[status] || status
  }

  const getSessionStatusBadge = (status: string) => {
    const badges: Record<string, string> = {
      draft: 'badge-secondary',
      running: 'badge-info',
      succeeded: 'badge-success',
      failed: 'badge-danger',
      archived: 'badge-secondary',
      backfilling: 'badge-warning',
    }
    return badges[status] || 'badge-secondary'
  }

  const getSessionStatusText = (status: string) => {
    const texts: Record<string, string> = {
      draft: 'Черновик',
      running: 'Выполняется',
      succeeded: 'Успешно',
      failed: 'Ошибка',
      archived: 'Архивирован',
      backfilling: 'Дозаполнение',
    }
    return texts[status] || status
  }

  const formatSessionDuration = (startedAt?: string | null, stoppedAt?: string | null) => {
    if (!startedAt) return '-'
    const start = new Date(startedAt)
    const end = stoppedAt ? new Date(stoppedAt) : new Date()
    const seconds = Math.floor((end.getTime() - start.getTime()) / 1000)
    return formatDuration(seconds)
  }

  if (isLoading) {
    return <div className="loading">Загрузка...</div>
  }

  if (error || !run) {
    return <div className="error">Запуск не найден</div>
  }

  return (
    <div className="run-detail">
      <div className="run-header card">
        <div className="card-header">
          <div>
            <h2 className="card-title">{run.name}</h2>
            <Link
              to={`/experiments/${run.experiment_id}`}
              className="experiment-link"
            >
              ← Вернуться к эксперименту
            </Link>
          </div>
          <div className="header-actions">
            <span className={`badge ${getStatusBadge(run.status)}`}>
              {getStatusText(run.status)}
            </span>
            {run.status === 'running' && (
              <>
                {!activeSession ? (
                  <button
                    className="btn btn-primary"
                    onClick={() => {
                      const notes = prompt('Заметки (опционально):')
                      createSessionMutation.mutate(notes || undefined)
                    }}
                    disabled={createSessionMutation.isPending || !experiment}
                  >
                    {createSessionMutation.isPending ? 'Создание...' : 'Старт отсчёта'}
                  </button>
                ) : (
                  <button
                    className="btn btn-danger"
                    onClick={() => {
                      if (confirm('Остановить отсчёт?')) {
                        stopSessionMutation.mutate(activeSession.id)
                      }
                    }}
                    disabled={stopSessionMutation.isPending}
                  >
                    {stopSessionMutation.isPending ? 'Остановка...' : 'Стоп отсчёта'}
                  </button>
                )}
                <button
                  className="btn btn-success"
                  onClick={() => completeMutation.mutate()}
                  disabled={completeMutation.isPending}
                >
                  Завершить
                </button>
                <button
                  className="btn btn-danger"
                  onClick={() => {
                    const reason = prompt('Причина ошибки:')
                    if (reason !== null) {
                      failMutation.mutate(reason)
                    }
                  }}
                  disabled={failMutation.isPending}
                >
                  Пометить как ошибка
                </button>
              </>
            )}
          </div>
        </div>

        <div className="run-info">
          <div className="info-row">
            <strong>ID:</strong>
            <span className="mono">{run.id}</span>
          </div>
          <div className="info-row">
            <strong>Experiment ID:</strong>
            <span className="mono">{run.experiment_id}</span>
          </div>
          <div className="info-row">
            <strong>Статус:</strong>
            <span>{getStatusText(run.status)}</span>
          </div>
          {run.started_at && (
            <div className="info-row">
              <strong>Начало:</strong>
              <span>
                {format(new Date(run.started_at), 'dd MMM yyyy HH:mm:ss')}
              </span>
            </div>
          )}
          {run.completed_at && (
            <div className="info-row">
              <strong>Завершение:</strong>
              <span>
                {format(new Date(run.completed_at), 'dd MMM yyyy HH:mm:ss')}
              </span>
            </div>
          )}
          {run.duration_seconds && (
            <div className="info-row">
              <strong>Длительность:</strong>
              <span>{formatDuration(run.duration_seconds)}</span>
            </div>
          )}
          <div className="info-row">
            <strong>Создан:</strong>
            <span>
              {format(new Date(run.created_at), 'dd MMM yyyy HH:mm')}
            </span>
          </div>
        </div>

        {run.notes && (
          <div className="notes-section">
            <h3>Заметки</h3>
            <p>{run.notes}</p>
          </div>
        )}

        <div className="parameters-section">
          <h3>Параметры запуска</h3>
          <pre className="parameters-json">
            {JSON.stringify(run.parameters, null, 2)}
          </pre>
        </div>

        {run.metadata && Object.keys(run.metadata).length > 0 && (
          <div className="metadata-section">
            <h3>Метаданные</h3>
            <pre className="metadata-json">
              {JSON.stringify(run.metadata, null, 2)}
            </pre>
          </div>
        )}
      </div>

      {/* Capture Sessions Section */}
      <div className="capture-sessions-section card">
        <div className="card-header">
          <h3>Сессии отсчёта</h3>
        </div>

        {sessionsLoading ? (
          <div className="loading">Загрузка сессий...</div>
        ) : sessions.length === 0 ? (
          <div className="empty-state">
            <p>Сессии отсчёта отсутствуют</p>
            {run.status === 'running' && !activeSession && experiment && (
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
          </div>
        ) : (
          <div className="sessions-list">
            {activeSession && (
              <div className="session-card active">
                <div className="session-header">
                  <div>
                    <h4>Активная сессия #{activeSession.ordinal_number}</h4>
                    <span className={`badge ${getSessionStatusBadge(activeSession.status)}`}>
                      {getSessionStatusText(activeSession.status)}
                    </span>
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
                    <div className="info-row">
                      <strong>Начало:</strong>
                      <span>
                        {format(new Date(activeSession.started_at), 'dd MMM yyyy HH:mm:ss')}
                      </span>
                    </div>
                    <div className="info-row">
                      <strong>Длительность:</strong>
                      <span>{formatSessionDuration(activeSession.started_at, activeSession.stopped_at)}</span>
                    </div>
                    {activeSession.notes && (
                      <div className="info-row">
                        <strong>Заметки:</strong>
                        <span>{activeSession.notes}</span>
                      </div>
                    )}
                  </div>
                )}
              </div>
            )}

            {sessions
              .filter((s: CaptureSession) => s.status !== 'running')
              .sort((a: CaptureSession, b: CaptureSession) => b.ordinal_number - a.ordinal_number)
              .map((session: CaptureSession) => (
                <div key={session.id} className="session-card">
                  <div className="session-header">
                    <div>
                      <h4>Сессия #{session.ordinal_number}</h4>
                      <span className={`badge ${getSessionStatusBadge(session.status)}`}>
                        {getSessionStatusText(session.status)}
                      </span>
                    </div>
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
                  <div className="session-info">
                    {session.started_at && (
                      <div className="info-row">
                        <strong>Начало:</strong>
                        <span>
                          {format(new Date(session.started_at), 'dd MMM yyyy HH:mm:ss')}
                        </span>
                      </div>
                    )}
                    {session.stopped_at && (
                      <div className="info-row">
                        <strong>Остановка:</strong>
                        <span>
                          {format(new Date(session.stopped_at), 'dd MMM yyyy HH:mm:ss')}
                        </span>
                      </div>
                    )}
                    {session.started_at && (
                      <div className="info-row">
                        <strong>Длительность:</strong>
                        <span>{formatSessionDuration(session.started_at, session.stopped_at)}</span>
                      </div>
                    )}
                    {session.notes && (
                      <div className="info-row">
                        <strong>Заметки:</strong>
                        <span>{session.notes}</span>
                      </div>
                    )}
                  </div>
                </div>
              ))}
          </div>
        )}
      </div>
    </div>
  )
}

export default RunDetail

