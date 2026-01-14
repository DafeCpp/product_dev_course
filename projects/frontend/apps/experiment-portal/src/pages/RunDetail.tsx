import { useEffect, useState } from 'react'
import { useParams, Link } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { runsApi, experimentsApi, captureSessionsApi, sensorsApi } from '../api/client'
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
import TelemetryStreamModal from '../components/TelemetryStreamModal'
import './RunDetail.css'
import { setActiveProjectId } from '../utils/activeProject'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'

function RunDetail() {
  const { id } = useParams<{ id: string }>()
  const queryClient = useQueryClient()
  const [actionError, setActionError] = useState<string | null>(null)
  const [optimisticActiveSessionId, setOptimisticActiveSessionId] = useState<string | null>(null)
  const [selectedSensorId, setSelectedSensorId] = useState<string>('')
  const [showTelemetryStream, setShowTelemetryStream] = useState(false)

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

  const { data: sensorsData, isLoading: sensorsLoading } = useQuery({
    queryKey: ['sensors', experiment?.project_id],
    queryFn: () => sensorsApi.list({ project_id: experiment!.project_id }),
    enabled: !!experiment?.project_id,
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
  const activeSession = sessions.find((s: CaptureSession) => s.status === 'running' || s.status === 'backfilling')
  const activeSessionId = activeSession?.id || optimisticActiveSessionId
  const sensors = sensorsData?.sensors || []

  // If backend/session list finally reflects active session, prefer real data and clear optimistic flag.
  useEffect(() => {
    if (activeSession?.id) {
      setOptimisticActiveSessionId(null)
    }
  }, [activeSession?.id])

  useEffect(() => {
    if (sensors.length === 0) return
    if (!selectedSensorId || !sensors.some((sensor) => sensor.id === selectedSensorId)) {
      setSelectedSensorId(sensors[0].id)
    }
  }, [sensors, selectedSensorId])

  const completeMutation = useMutation({
    mutationFn: () => runsApi.complete(id!),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['run', id] })
      queryClient.invalidateQueries({ queryKey: ['runs'] })
      notifySuccess('Run завершён')
    },
    onError: (err: any) => {
      const msg =
        err?.response?.data?.message ||
        err?.response?.data?.error ||
        err?.message ||
        'Не удалось завершить run'
      setActionError(msg)
      notifyError(msg)
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
      notifySuccess('Run запущен')
    },
    onError: (err: any) => {
      const msg =
        err?.response?.data?.message ||
        err?.response?.data?.error ||
        err?.message ||
        'Не удалось запустить run'
      setActionError(msg)
      notifyError(msg)
    },
  })

  const failMutation = useMutation({
    mutationFn: (reason?: string) => runsApi.fail(id!, reason),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['run', id] })
      queryClient.invalidateQueries({ queryKey: ['runs'] })
      notifySuccess('Run помечен как failed')
    },
    onError: (err: any) => {
      const msg =
        err?.response?.data?.message ||
        err?.response?.data?.error ||
        err?.message ||
        'Не удалось пометить run как failed'
      setActionError(msg)
      notifyError(msg)
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
      notifySuccess('Отсчёт запущен')
    },
    onError: (err: any) => {
      const msg =
        err?.response?.data?.message ||
        err?.response?.data?.error ||
        err?.message ||
        'Не удалось запустить отсчёт'
      setActionError(msg)
      notifyError(msg)
    },
  })

  // Остановка capture session
  const stopSessionMutation = useMutation({
    mutationFn: (sessionId: string) => captureSessionsApi.stop(id!, sessionId),
    onSuccess: () => {
      setOptimisticActiveSessionId(null)
      queryClient.invalidateQueries({ queryKey: ['capture-sessions', id] })
      notifySuccess('Отсчёт остановлен')
    },
    onError: (err: any) => {
      const msg =
        err?.response?.data?.message ||
        err?.response?.data?.error ||
        err?.message ||
        'Не удалось остановить отсчёт'
      setActionError(msg)
      notifyError(msg)
    },
  })

  // Удаление capture session
  const deleteSessionMutation = useMutation({
    mutationFn: (sessionId: string) => captureSessionsApi.delete(id!, sessionId),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['capture-sessions', id] })
      notifySuccess('Сессия удалена')
    },
    onError: (err: any) => {
      const msg =
        err?.response?.data?.message ||
        err?.response?.data?.error ||
        err?.message ||
        'Не удалось удалить сессию'
      setActionError(msg)
      notifyError(msg)
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
    return IS_TEST ? <ErrorComponent message="Запуск не найден" /> : <EmptyState message="Запуск не найден" />
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
            <div className="run-detail__primary-actions">
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
            </div>
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

        {canManageSessions && (
          <div className="run-detail__countdown-actions">
            <button
              className={`btn ${activeSessionId ? 'btn-danger' : 'btn-primary'}`}
              onClick={() => {
                if (!activeSessionId) {
                  const notes = prompt('Заметки (опционально):')
                  createSessionMutation.mutate(notes || undefined)
                  return
                }
                if (confirm('Остановить отсчёт?')) {
                  stopSessionMutation.mutate(activeSessionId)
                }
              }}
              disabled={
                activeSessionId
                  ? stopSessionMutation.isPending
                  : createSessionMutation.isPending || !experiment
              }
            >
              {activeSessionId
                ? (stopSessionMutation.isPending ? 'Остановка...' : 'Остановить отсчёт')
                : (createSessionMutation.isPending ? 'Создание...' : 'Старт отсчёта')}
            </button>
          </div>
        )}

        {IS_TEST && actionError && (
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

      {/* Telemetry Section */}
      <div className="telemetry-section card">
        <div className="card-header">
          <h3>Телеметрия</h3>
        </div>
        {sensorsLoading ? (
          <Loading message="Загрузка датчиков..." />
        ) : sensors.length === 0 ? (
          <EmptyState message="В проекте нет датчиков" />
        ) : (
          <>
            <div className="telemetry-controls">
              <div className="form-group">
                <label htmlFor="run-telemetry-sensor">Датчик</label>
                <select
                  id="run-telemetry-sensor"
                  value={selectedSensorId}
                  onChange={(e) => setSelectedSensorId(e.target.value)}
                >
                  {sensors.map((sensor) => (
                    <option key={sensor.id} value={sensor.id}>
                      {sensor.name} ({sensor.type})
                    </option>
                  ))}
                </select>
              </div>
              <div className="telemetry-actions">
                <button
                  className="btn btn-primary"
                  onClick={() => setShowTelemetryStream(true)}
                  disabled={!selectedSensorId}
                >
                  Live telemetry
                </button>
              </div>
            </div>
            <div className="telemetry-hints">
              <span className="mono">run_id: {run.id}</span>
              {activeSessionId && (
                <span className="mono">capture_session_id: {activeSessionId}</span>
              )}
            </div>
          </>
        )}
      </div>

      {selectedSensorId && (
        <TelemetryStreamModal
          sensorId={selectedSensorId}
          isOpen={showTelemetryStream}
          onClose={() => setShowTelemetryStream(false)}
          filterRunId={run.id}
          filterCaptureSessionId={activeSessionId || undefined}
        />
      )}

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

