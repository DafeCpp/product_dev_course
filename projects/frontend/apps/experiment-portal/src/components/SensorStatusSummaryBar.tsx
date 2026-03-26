import type { StatusSummary } from '../types'
import './SensorStatusSummaryBar.scss'

interface SensorStatusSummaryBarProps {
  summary: StatusSummary
}

function SensorStatusSummaryBar({ summary }: SensorStatusSummaryBarProps) {
  return (
    <div className="status-summary-bar card">
      <span className="status-summary-bar__label">Состояние флота</span>
      <div className="status-summary-bar__chips">
        <span className="status-chip status-chip--online">
          <span className="status-chip__dot" aria-hidden="true" />
          Онлайн: <strong>{summary.online}</strong>
        </span>
        <span className="status-chip status-chip--delayed">
          <span className="status-chip__dot" aria-hidden="true" />
          Задержка: <strong>{summary.delayed}</strong>
        </span>
        <span className="status-chip status-chip--offline">
          <span className="status-chip__dot" aria-hidden="true" />
          Оффлайн: <strong>{summary.offline}</strong>
        </span>
        <span className="status-chip status-chip--total">
          Всего: <strong>{summary.total}</strong>
        </span>
      </div>
    </div>
  )
}

export default SensorStatusSummaryBar
