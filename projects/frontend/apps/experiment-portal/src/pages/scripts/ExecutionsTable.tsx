import { format } from 'date-fns'
import type { ScriptExecution } from '../../types/scripts'
import { executionStatusClass, executionStatusLabel, calcDuration } from './utils'

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
              role="button"
              tabIndex={0}
              onKeyDown={(e) => {
                if (e.key === 'Enter' || e.key === ' ') onRowClick(ex)
              }}
            >
              <td className="scripts-table__name">{scriptName(ex.script_id)}</td>
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
                  ? format(new Date(ex.started_at), 'dd MMM HH:mm:ss')
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
