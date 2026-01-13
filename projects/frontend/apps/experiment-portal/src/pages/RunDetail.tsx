import { useEffect, useState } from 'react'
import { useParams, Link } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { runsApi, experimentsApi, captureSessionsApi } from '../api/client'
import { format } from 'date-fns'
import type { CaptureSession } from '../types'
import {
  StatusBadge,
  Loading,
  Error as ErrorComponent,
  EmptyState,
  InfoRow,
  runStatusMap,
  captureSessionStatusMap,
} from '../components/common'
import './RunDetail.css'
import { setActiveProjectId } from '../utils/activeProject'

function RunDetail() {
  const { id } = useParams<{ id: string }>()
  const queryClient = useQueryClient()
  const [actionError, setActionError] = useState<string | null>(null)

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

  // В experiment-service project_id обязателен: подстраиваем локальный "active project"
  // под run/experiment, чтобы PATCH/POST работали консистентно (особенно при переходе по прямой ссылке).
  useEffect(() => {
    if (!experiment?.project_id) return
    setActiveProjectId(experiment.project_id)
  }, [experiment?.project_id])

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
    onError: (err: any) => {
      setActionError(err?.response?.data?.message || err?.response?.data?.error || err?.message || 'Не удалось завершить run')
    },
  })

  const startRunMutation = useMutation({
    mutationFn: () => {
      setActionError(null)
      return runsApi.update(id!, { status: 'running' })
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['run', id] })
      queryClient.invalidateQueries({ queryKey: ['runs'] })
    },
    onError: (err: any) => {
      setActionError(err?.response?.data?.message || err?.response?.data?.error || err?.message || 'Не удалось запустить run')
    },
  })

  const failMutation = useMutation({
    mutationFn: (reason?: string) => runsApi.fail(id!, reason),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['run', id] })
      queryClient.invalidateQueries({ queryKey: ['runs'] })
    },
    onError: (err: any) => {
      setActionError(err?.response?.data?.message || err?.response?.data?.error || err?.message || 'Не удалось пометить run как failed')
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


  const formatSessionDuration = (startedAt?: string | null, stoppedAt?: string | null) => {
    if (!startedAt) return '-'
    const start = new Date(startedAt)
    const end = stoppedAt ? new Date(stoppedAt) : new Date()
    const seconds = Math.floor((end.getTime() - start.getTime()) / 1000)
    return formatDuration(seconds)
  }

  if (isLoading) {
    return <Loading />
  }

  if (error || !run) {
    return <ErrorComponent message="Запуск не найден" />
  }

  // UI note:
  // Backend позволяет создавать capture sessions, даже если run ещё в статусе draft.
  // Поэтому разрешаем "Старт/Стоп отсчёта" для draft+running (а не только running),
  // иначе ручной сценарий ломается без отдельной кнопки "Start run".
  const canManageSessions = run.status === 'draft' || run.status === 'running'

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
            <StatusBadge status={run.status} statusMap={runStatusMap} />
            {run.status === 'draft' && (
              <button
                className="btn btn-primary"
                onClick={() => startRunMutation.mutate()}
                disabled={startRunMutation.isPending || !experiment}
                title={!experiment ? 'Загрузка эксперимента (project_id)...' : undefined}
              >
                {startRunMutation.isPending ? 'Запуск...' : 'Запустить'}
              </button>
            )}
            {canManageSessions && (
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
              </>
            )}
            {run.status === 'running' && (
              <>
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

        {actionError && (
          <div className="error" style={{ marginTop: '0.75rem' }}>
            {actionError}
          </div>
        )}

        <div className="run-info">
          <InfoRow label="ID" value={<span className="mono">{run.id}</span>} />
          <InfoRow
            label="Experiment ID"
            value={<span className="mono">{run.experiment_id}</span>}
          />
          <InfoRow
            label="Статус"
            value={<StatusBadge status={run.status} statusMap={runStatusMap} />}
          />
          {run.started_at && (
            <InfoRow
              label="Начало"
              value={format(new Date(run.started_at), 'dd MMM yyyy HH:mm:ss')}
            />
          )}
          {run.finished_at && (
            <InfoRow
              label="Завершение"
              value={format(new Date(run.finished_at), 'dd MMM yyyy HH:mm:ss')}
            />
          )}
          {run.duration_seconds && (
            <InfoRow label="Длительность" value={formatDuration(run.duration_seconds)} />
          )}
          <InfoRow
            label="Создан"
            value={format(new Date(run.created_at), 'dd MMM yyyy HH:mm')}
          />
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
            {JSON.stringify(run.params, null, 2)}
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
                    <StatusBadge
                      status={activeSession.status}
                      statusMap={captureSessionStatusMap}
                    />
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
                    <InfoRow
                      label="Начало"
                      value={format(new Date(activeSession.started_at), 'dd MMM yyyy HH:mm:ss')}
                    />
                    <InfoRow
                      label="Длительность"
                      value={formatSessionDuration(
                        activeSession.started_at,
                        activeSession.stopped_at
                      )}
                    />
                    {activeSession.notes && (
                      <InfoRow label="Заметки" value={activeSession.notes} />
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
                      <StatusBadge status={session.status} statusMap={captureSessionStatusMap} />
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
                      <InfoRow
                        label="Начало"
                        value={format(new Date(session.started_at), 'dd MMM yyyy HH:mm:ss')}
                      />
                    )}
                    {session.stopped_at && (
                      <InfoRow
                        label="Остановка"
                        value={format(new Date(session.stopped_at), 'dd MMM yyyy HH:mm:ss')}
                      />
                    )}
                    {session.started_at && (
                      <InfoRow
                        label="Длительность"
                        value={formatSessionDuration(session.started_at, session.stopped_at)}
                      />
                    )}
                    {session.notes && <InfoRow label="Заметки" value={session.notes} />}
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

