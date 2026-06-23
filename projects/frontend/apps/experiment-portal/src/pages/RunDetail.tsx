import { useEffect, useMemo, useState } from 'react'
import { useParams, Link } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import { useApiMutation } from '../hooks/useApiMutation'
import { runsApi, experimentsApi, captureSessionsApi, sensorsApi } from '../api/client'
import { format } from 'date-fns'
import type { CaptureSession } from '../types'
import {
  StatusBadge,
  Loading,
  Error as ErrorComponent,
  EmptyState,
  MaterialSelect,
  runStatusMap,
} from '../components/common'
import TelemetryStreamModal from '../components/TelemetryStreamModal'
import TelemetryExportModal from '../components/TelemetryExportModal'
import AuditLog from '../components/AuditLog'
import RunMetrics from '../components/RunMetrics'
import ArtifactsPanel from '../components/ArtifactsPanel'
import RunSensorsPanel from './run-detail/RunSensorsPanel'
import RunOverviewGrid from './run-detail/RunOverviewGrid'
import RunCaptureSessions from './run-detail/RunCaptureSessions'
import './RunDetail.scss'
import { setActiveProjectId } from '../utils/activeProject'
import { IS_TEST } from '../utils/env'
import { useCountdown } from '../hooks/useCountdown'

function RunDetail() {
  const { id } = useParams<{ id: string }>()
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

  const completeMutation = useApiMutation({
    mutationFn: () => runsApi.complete(id!),
    invalidateKeys: [['run', id], ['runs']],
    successMessage: 'Run завершён',
    errorFallback: 'Не удалось завершить run',
    onError: (err: any) => setActionError(err?.response?.data?.error || err?.response?.data?.message || err?.message || 'Не удалось завершить run'),
  })

  const startRunMutation = useApiMutation({
    mutationFn: () => {
      setActionError(null)
      return runsApi.update(id!, { status: 'running' })
    },
    invalidateKeys: [['run', id], ['runs']],
    successMessage: 'Run запущен',
    errorFallback: 'Не удалось запустить run',
    onError: (err: any) => setActionError(err?.response?.data?.error || err?.response?.data?.message || err?.message || 'Не удалось запустить run'),
  })

  const failMutation = useApiMutation<unknown, string | undefined>({
    mutationFn: (reason) => runsApi.fail(id!, reason),
    invalidateKeys: [['run', id], ['runs']],
    successMessage: 'Run помечен как failed',
    errorFallback: 'Не удалось пометить run как failed',
    onError: (err: any) => setActionError(err?.response?.data?.error || err?.response?.data?.message || err?.message || 'Не удалось пометить run как failed'),
  })

  // Создание capture session
  const createSessionMutation = useApiMutation<unknown, string | undefined>({
    mutationFn: (notes) => {
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
    invalidateKeys: [['capture-sessions', id]],
    successMessage: 'Отсчёт запущен',
    errorFallback: 'Не удалось запустить отсчёт',
    onError: (err: any) => setActionError(err?.response?.data?.error || err?.response?.data?.message || err?.message || 'Не удалось запустить отсчёт'),
  })

  // Остановка capture session
  const stopSessionMutation = useApiMutation<unknown, string>({
    mutationFn: (sessionId) => captureSessionsApi.stop(id!, sessionId),
    invalidateKeys: [['capture-sessions', id]],
    successMessage: 'Отсчёт остановлен',
    errorFallback: 'Не удалось остановить отсчёт',
    onSuccess: () => setOptimisticActiveSessionId(null),
    onError: (err: any) => setActionError(err?.response?.data?.error || err?.response?.data?.message || err?.message || 'Не удалось остановить отсчёт'),
  })

  // Удаление capture session
  const deleteSessionMutation = useApiMutation<unknown, string>({
    mutationFn: (sessionId) => captureSessionsApi.delete(id!, sessionId),
    invalidateKeys: [['capture-sessions', id]],
    successMessage: 'Сессия удалена',
    errorFallback: 'Не удалось удалить сессию',
    onError: (err: any) => setActionError(err?.response?.data?.error || err?.response?.data?.message || err?.message || 'Не удалось удалить сессию'),
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

      <RunOverviewGrid
        run={run}
        sensors={sensors}
        activeSession={activeSession}
        actionError={actionError}
        formattedStartedAt={formattedStartedAt}
        formattedFinishedAt={formattedFinishedAt}
        formattedCreatedAt={formattedCreatedAt}
        formattedDuration={formattedDuration}
      />

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

      <RunCaptureSessions
        runId={id!}
        sessions={sessions}
        activeSession={activeSession}
        sessionsLoading={sessionsLoading}
        canManageSessions={canManageSessions}
        experiment={experiment}
        createSessionMutation={createSessionMutation}
        stopSessionMutation={stopSessionMutation}
        deleteSessionMutation={deleteSessionMutation}
        setExportTarget={setExportTarget}
        formatSessionDuration={formatSessionDuration}
      />

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
