import { format } from 'date-fns'
import type { SensorErrorEntry, SensorErrorLogResponse } from '../../types'
import { Loading } from '../../components/common'

interface SensorErrorLogProps {
  errorLogData: SensorErrorLogResponse | undefined
  isLoadingErrorLog: boolean
  errorLogPage: number
  setErrorLogPage: (updater: (prev: number) => number) => void
  errorLogLimit: number
}

export default function SensorErrorLog({
  errorLogData,
  isLoadingErrorLog,
  errorLogPage,
  setErrorLogPage,
  errorLogLimit,
}: SensorErrorLogProps) {
  return (
    <div className="sensor-error-log-section">
      <div className="section-header">
        <h3>
          Журнал ошибок
          {errorLogData && errorLogData.total > 0 && (
            <span className="badge badge-danger" style={{ marginLeft: 8 }}>
              {errorLogData.total}
            </span>
          )}
        </h3>
      </div>

      {isLoadingErrorLog && <Loading />}

      {!isLoadingErrorLog && errorLogData && errorLogData.entries.length > 0 && (
        <>
          <div className="table-responsive">
            <table>
              <thead>
                <tr>
                  <th>Время</th>
                  <th>Код ошибки</th>
                  <th>Канал</th>
                  <th>Чтений</th>
                  <th>Сообщение</th>
                </tr>
              </thead>
              <tbody>
                {errorLogData.entries.map((entry: SensorErrorEntry) => (
                  <tr key={entry.id}>
                    <td style={{ whiteSpace: 'nowrap' }}>
                      {format(new Date(entry.occurred_at), 'dd MMM HH:mm:ss')}
                    </td>
                    <td>
                      <span className={`badge ${
                        entry.error_code === 'rate_limited' ? 'badge-warning' :
                        entry.error_code === 'unauthorized' ? 'badge-danger' :
                        entry.error_code === 'validation_error' ? 'badge-info' :
                        'badge-secondary'
                      }`}>
                        {entry.error_code}
                      </span>
                    </td>
                    <td>{entry.endpoint}</td>
                    <td>{entry.readings_count ?? '—'}</td>
                    <td className="text-muted" style={{ maxWidth: 300, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                      {entry.error_message ?? '—'}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          {errorLogData.total > errorLogLimit && (
            <div className="pagination-row" style={{ marginTop: 8 }}>
              <button
                className="btn btn-sm btn-secondary"
                disabled={errorLogPage === 0}
                onClick={() => setErrorLogPage(p => p - 1)}
              >
                ← Назад
              </button>
              <span style={{ fontSize: '0.85rem', color: 'var(--text-secondary)' }}>
                {errorLogPage * errorLogLimit + 1}–{Math.min((errorLogPage + 1) * errorLogLimit, errorLogData.total)} из {errorLogData.total}
              </span>
              <button
                className="btn btn-sm btn-secondary"
                disabled={(errorLogPage + 1) * errorLogLimit >= errorLogData.total}
                onClick={() => setErrorLogPage(p => p + 1)}
              >
                Вперёд →
              </button>
            </div>
          )}
        </>
      )}

      {!isLoadingErrorLog && errorLogData && errorLogData.entries.length === 0 && (
        <p className="text-muted">Ошибок нет</p>
      )}
    </div>
  )
}
