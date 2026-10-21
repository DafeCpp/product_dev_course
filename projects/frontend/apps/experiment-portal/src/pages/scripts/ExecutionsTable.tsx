import type { ScriptExecution } from '../../types/scripts'
import { executionStatusClass, executionStatusLabel, calcDuration } from './utils'
import { formatShortDateTime } from '../../utils/format'

export interface ExecutionsTableProps {
  executions: ScriptExecution[]
  scriptName: (id: string) => string
  onRowClick: (execution: ScriptExecution) => void
}

export default function ExecutionsTable({
  executions,
  scriptName,
  onRowClick,
}: ExecutionsTableProps) {
  return (
    <div className="scripts-table-wrap card">
      <table className="scripts-table">
        <thead>
          <tr>
            <th>Скрипт</th>
            <th>Статус</th>
            <th>Запросил</th>
            <th>Сервис</th>
            <th>Начало</th>
            <th>Длительность</th>
          </tr>
        </thead>
        <tbody>
          {executions.map((ex) => (
            <tr
              key={ex.id}
              className="scripts-table__row"
              onClick={() => onRowClick(ex)}
            >
              <td className="scripts-table__name">
                {/* Keyboard entry point to the row; its click bubbles to the row's handler. */}
                <button type="button" className="table-row-open">
                  {scriptName(ex.script_id)}
                </button>
              </td>
              <td>
                <span className={`exec-status ${executionStatusClass(ex.status)}`}>
                  {executionStatusLabel(ex.status)}
                </span>
              </td>
              <td>{ex.requested_by}</td>
              <td>
                <span className="scripts-table__service">
                  {ex.target_instance ?? '—'}
                </span>
              </td>
              <td className="scripts-table__time">
                {ex.started_at
                  ? formatShortDateTime(ex.started_at, { seconds: true })
                  : '—'}
              </td>
              <td>{calcDuration(ex.started_at, ex.finished_at)}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}
