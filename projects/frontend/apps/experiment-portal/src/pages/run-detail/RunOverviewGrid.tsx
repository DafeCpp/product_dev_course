import type { Run, CaptureSession, Sensor } from '../../types'
import { StatusBadge, InfoRow, runStatusMap } from '../../components/common'
import { IS_TEST } from '../../utils/env'

interface RunOverviewGridProps {
  run: Run
  sensors: Sensor[]
  activeSession: CaptureSession | undefined
  actionError: string | null
  formattedStartedAt: string
  formattedFinishedAt: string
  formattedCreatedAt: string
  formattedDuration: string
}

export default function RunOverviewGrid({
  run,
  sensors,
  activeSession,
  actionError,
  formattedStartedAt,
  formattedFinishedAt,
  formattedCreatedAt,
  formattedDuration,
}: RunOverviewGridProps) {
  return (
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
  )
}
