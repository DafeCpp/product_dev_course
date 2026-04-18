import { useEffect, useMemo, useState } from 'react'
import { useParams, Link } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { runsApi, experimentsApi, captureSessionsApi, sensorsApi, runSensorsApi } from '../api/client'
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
  MaterialSelect,
} from '../components/common'
import TelemetryStreamModal from '../components/TelemetryStreamModal'
import TelemetryExportModal from '../components/TelemetryExportModal'
import AuditLog from '../components/AuditLog'
import RunMetrics from '../components/RunMetrics'
import ArtifactsPanel from '../components/ArtifactsPanel'
import './RunDetail.scss'
import { setActiveProjectId } from '../utils/activeProject'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'
import { useCountdown } from '../hooks/useCountdown'

// ---------------------------------------------------------------------------
// Run Sensors panel
// ---------------------------------------------------------------------------

function RunSensorsPanel({ runId, projectId }: { runId: string; projectId: string }) {
  const queryClient = useQueryClient()
  const [showAdd, setShowAdd] = useState(false)

  const { data: attachedData, isLoading } = useQuery({
    queryKey: ['run-sensors', runId],
    queryFn: () => runSensorsApi.list(runId, { project_id: projectId }),
    enabled: !!runId,
  })

  const { data: allSensorsData } = useQuery({
    queryKey: ['sensors', projectId],
    queryFn: () => sensorsApi.list({ project_id: projectId }),
    enabled: !!projectId,
  })

  const attachMutation = useMutation({
    mutationFn: (sensorId: string) =>
      runSensorsApi.attach(runId, sensorId, { project_id: projectId }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['run-sensors', runId] })
      setShowAdd(false)
      notifySuccess('Датчик привязан')
    },
    onError: () => notifyError('Не удалось привязать датчик'),
  })

  const detachMutation = useMutation({
    mutationFn: (sensorId: string) =>
      runSensorsApi.detach(runId, sensorId, { project_id: projectId }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['run-sensors', runId] })
      notifySuccess('Датчик откреплён')
    },
    onError: () => notifyError('Не удалось открепить датчик'),
  })

  const attached = attachedData?.sensors ?? []
  const attachedIds = new Set(attached.map((s) => s.sensor_id))
  const allSensors = allSensorsData?.sensors ?? []
  const available = allSensors.filter((s) => !attachedIds.has(s.id))

  if (isLoading) return <Loading />

  return (
    <div className="run-sensors">
      {attached.length === 0 ? (
        <p className="run-sensors__empty">Датчики не привязаны</p>
      ) : (
        <div className="run-sensors__list">
          {attached.map((rs) => {
            const sensor = allSensors.find((s) => s.id === rs.sensor_id)
            return (
              <div key={rs.sensor_id} className="run-sensors__item">
                <span className="run-sensors__name">{sensor?.name ?? rs.sensor_id}</span>
                <span className="run-sensors__type">{sensor?.type ?? '—'}</span>
                <span className="run-sensors__mode badge">{rs.mode}</span>
                <button
                  className="btn btn-danger btn-sm"
                  onClick={() => detachMutation.mutate(rs.sensor_id)}
                  disabled={detachMutation.isPending}
                >
                  Открепить
                </button>
              </div>
            )
          })}
        </div>
      )}
      {!showAdd ? (
        <button
          className="btn btn-secondary btn-sm"
          onClick={() => setShowAdd(true)}
          disabled={available.length === 0}
          style={{ marginTop: '0.5rem' }}
        >
          + Привязать датчик
        </button>
      ) : (
        <div className="run-sensors__add" style={{ marginTop: '0.5rem', display: 'flex', gap: '0.5rem' }}>
          <select
            defaultValue=""
            onChange={(e) => { if (e.target.value) attachMutation.mutate(e.target.value) }}
            disabled={attachMutation.isPending}
          >
            <option value="">Выберите датчик...</option>
            {available.map((s) => (
              <option key={s.id} value={s.id}>{s.name} ({s.type})</option>
            ))}
          </select>
          <button className="btn btn-secondary btn-sm" onClick={() => setShowAdd(false)}>
            Отмена
          </button>
        </div>
      )}
    </div>
  )
}

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

  const countdownDeadline = useMemo(() => {
    if (
      !run ||
      run.status !== 'running' ||
      run.auto_complete_after_minutes === null ||
      !run.started_at
    ) {
      return null
    }
    return new Date(run.started_at).getTime() + run.auto_complete_after_minutes * 60 * 1000
  }, [run?.status, run?.auto_complete_after_minutes, run?.started_at])

  const { remaining: countdownRemaining, isWarning: countdownIsWarning, isExpired: countdownIsExpired } =
    useCountdown(countdownDeadline)

  const formatCountdown = (ms: number): string => {
    const totalSeconds = Math.max(0, Math.ceil(ms / 1000))
    const minutes = Math.floor(totalSeconds / 60)
    const seconds = totalSeconds % 60
    return `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`
  }

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

  // Состояние диалога экспорта телеметрии
  type ExportTarget = { mode: 'session'; sessionId: string; sessionOrdinal: number } | { mode: 'run' }
  const [exportTarget, setExportTarget] = useState<ExportTarget | null>(null)

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
  const formattedStartedAt = run.started_at
    ? format(new Date(run.started_at), 'dd MMM yyyy HH:mm:ss')
    : '—'
  const formattedFinishedAt = run.finished_at
    ? format(new Date(run.finished_at), 'dd MMM yyyy HH:mm:ss')
    : '—'
  const formattedCreatedAt = format(new Date(run.created_at), 'dd MMM yyyy HH:mm')
  const formattedDuration = run.duration_seconds ? formatDuration(run.duration_seconds) : '—'
  const completedSessionsCount = sessions.filter(
    (session: CaptureSession) => session.status !== 'running' && session.status !== 'backfilling'
  ).length

  return (
    <div className="run-detail detail-page">
      <section className="compact-page-header card">
        <div className="compact-page-header__top">
          <div className="compact-page-header__main">
            <div className="compact-page-header__eyebrow">Run Detail</div>
            <Link to={`/experiments/${run.experiment_id}`} className="experiment-link detail-link">
              ← Вернуться к эксперименту
            </Link>
            <div className="compact-page-header__title-row">
              <h2 className="compact-page-header__title">{run.name}</h2>
              <StatusBadge status={run.status} statusMap={runStatusMap} />
            </div>
            <p className="compact-page-header__description">
              Операционный срез запуска: статус, capture sessions, метрики, потоковая телеметрия и аудит событий.
            </p>
          </div>
          <div className="compact-page-header__actions">
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
            {canManageSessions && (
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
                  ? stopSessionMutation.isPending
                    ? 'Остановка...'
                    : 'Остановить отсчёт'
                  : createSessionMutation.isPending
                    ? 'Создание...'
                    : 'Старт отсчёта'}
              </button>
            )}
          </div>
        </div>
        <div className="compact-page-header__meta">
          <span className="meta-chip">start: {formattedStartedAt}</span>
          <span className="meta-chip">sensors: {sensors.length}</span>
          <span className="meta-chip">
            sessions: {sessions.length}
            {activeSession
              ? ` / active #${activeSession.ordinal_number}`
              : ` / done ${completedSessionsCount}`}
          </span>
        </div>

        {countdownDeadline !== null && (
          <div className={`run-countdown${countdownIsWarning ? ' countdown-warning' : ''}`}>
            {countdownIsExpired
              ? 'Автозавершение ожидается...'
              : `Автозавершение через ${formatCountdown(countdownRemaining!)}`}
          </div>
        )}
      </section>

      <div className="detail-grid run-detail__overview-grid">
        <div className="run-header card detail-card">
          <div className="detail-section-header">
            <div className="detail-section-header__copy">
              <span className="detail-card__eyebrow">Run Record</span>
              <h3 className="detail-card__title">Контекст запуска</h3>
              <p>Основные идентификаторы, временные метки и статусные поля для диагностики и аудита.</p>
            </div>
          </div>

          {IS_TEST && actionError && <div className="error run-detail__action-error">{actionError}</div>}

          <div className="run-info">
            <InfoRow label="ID" value={<span className="mono">{run.id}</span>} />
            <InfoRow label="Experiment ID" value={<span className="mono">{run.experiment_id}</span>} />
            <InfoRow label="Статус" value={<StatusBadge status={run.status} statusMap={runStatusMap} />} />
            {run.started_at && <InfoRow label="Начало" value={formattedStartedAt} />}
            {run.finished_at && <InfoRow label="Завершение" value={formattedFinishedAt} />}
            {run.duration_seconds && <InfoRow label="Длительность" value={formattedDuration} />}
            <InfoRow label="Создан" value={formattedCreatedAt} />
          </div>

          <div className="detail-meta-grid">
            <div className="detail-meta-card">
              <span>Активная сессия</span>
              <strong>{activeSession ? `#${activeSession.ordinal_number}` : 'Нет'}</strong>
            </div>
            <div className="detail-meta-card">
              <span>Сенсоров</span>
              <strong>{sensors.length}</strong>
            </div>
            <div className="detail-meta-card">
              <span>Старт</span>
              <strong>{formattedStartedAt}</strong>
            </div>
            <div className="detail-meta-card">
              <span>Финиш</span>
              <strong>{formattedFinishedAt}</strong>
            </div>
          </div>
        </div>

        <div className="detail-stack">
          {run.notes && (
            <div className="notes-section card detail-card">
              <span className="detail-card__eyebrow">Notes</span>
              <h3 className="detail-card__title">Заметки</h3>
              <p className="detail-card__text">{run.notes}</p>
            </div>
          )}

          <div className="parameters-section card detail-card">
            <span className="detail-card__eyebrow">Params</span>
            <h3 className="detail-card__title">Параметры запуска</h3>
            <pre className="detail-code-block parameters-json">{JSON.stringify(run.params, null, 2)}</pre>
          </div>

          {run.metadata && Object.keys(run.metadata).length > 0 && (
            <div className="metadata-section card detail-card">
              <span className="detail-card__eyebrow">Metadata</span>
              <h3 className="detail-card__title">Метаданные</h3>
              <pre className="detail-code-block metadata-json">
                {JSON.stringify(run.metadata, null, 2)}
              </pre>
            </div>
          )}
        </div>
      </div>

      <section className="metrics-section card detail-card">
        <div className="detail-section-header">
          <div className="detail-section-header__copy">
            <span className="detail-card__eyebrow">Metrics</span>
            <h3 className="detail-card__title">Метрики</h3>
            <p>Визуальный срез записанных значений по текущему запуску.</p>
          </div>
        </div>
        <RunMetrics runId={id!} />
      </section>

      <section className="telemetry-section card detail-card">
        <div className="detail-section-header">
          <div className="detail-section-header__copy">
            <span className="detail-card__eyebrow">Telemetry</span>
            <h3 className="detail-card__title">Телеметрия</h3>
            <p>Открывайте live-поток по конкретному датчику и фильтруйте события в рамках запуска.</p>
          </div>
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
                <MaterialSelect
                  id="run-telemetry-sensor"
                  value={selectedSensorId}
                  onChange={(value) => setSelectedSensorId(value)}
                >
                  {sensors.map((sensor) => (
                    <option key={sensor.id} value={sensor.id}>
                      {sensor.name} ({sensor.type})
                    </option>
                  ))}
                </MaterialSelect>
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
              {activeSessionId && <span className="mono">capture_session_id: {activeSessionId}</span>}
            </div>
          </>
        )}
      </section>

      {selectedSensorId && (
        <TelemetryStreamModal
          sensorId={selectedSensorId}
          isOpen={showTelemetryStream}
          onClose={() => setShowTelemetryStream(false)}
          filterRunId={run.id}
          filterCaptureSessionId={activeSessionId || undefined}
        />
      )}

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
                  runId={id!}
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
                    runId={id!}
                    captureSessionId={session.id}
                    title={`События сессии #${session.ordinal_number}`}
                  />
                </div>
              ))}
          </div>
        )}
      </section>

      <section className="card detail-card">
        <div className="detail-section-header">
          <div className="detail-section-header__copy">
            <span className="detail-card__eyebrow">Sensors</span>
            <h3 className="detail-card__title">Датчики запуска</h3>
            <p>Датчики, привязанные к этому запуску для аннотации телеметрии.</p>
          </div>
        </div>
        <RunSensorsPanel runId={id!} projectId={experiment?.project_id ?? ''} />
      </section>

      <section className="card detail-card">
        <div className="detail-section-header">
          <div className="detail-section-header__copy">
            <span className="detail-card__eyebrow">Files</span>
            <h3 className="detail-card__title">Артефакты</h3>
            <p>Файлы, связанные с запуском: модели, датасеты, логи, графики и конфиги.</p>
          </div>
        </div>
        <ArtifactsPanel
          runId={id!}
          projectId={experiment?.project_id ?? ''}
          isOwner={false}
        />
      </section>

      <section className="audit-section card detail-card">
        <div className="detail-section-header">
          <div className="detail-section-header__copy">
            <span className="detail-card__eyebrow">Audit Trail</span>
            <h3 className="detail-card__title">Аудит-лог</h3>
            <p>Полная история событий запуска для отладки, расследования инцидентов и ретроспектив.</p>
          </div>
        </div>
        <AuditLog runId={id!} title="История событий запуска" />
      </section>

      {exportTarget && (
        <TelemetryExportModal
          isOpen
          onClose={() => setExportTarget(null)}
          runId={id!}
          mode={exportTarget.mode}
          sessionId={exportTarget.mode === 'session' ? exportTarget.sessionId : undefined}
          sessionOrdinal={exportTarget.mode === 'session' ? exportTarget.sessionOrdinal : undefined}
          sessions={sessions}
          sensors={sensors}
        />
      )}
    </div>
  )
}

export default RunDetail
