import { format } from 'date-fns'
import type { BackfillTask, BackfillTaskStatus, BackfillTasksListResponse } from '../../types'
import { Loading } from '../../components/common'

interface BackfillMutation {
  mutate: () => void
  isPending: boolean
}

interface SensorBackfillSectionProps {
  backfillData: BackfillTasksListResponse | undefined
  isLoadingBackfill: boolean
  startBackfillMutation: BackfillMutation
  hasActiveProfile: boolean
}

const backfillStatusLabels: Record<BackfillTaskStatus, string> = {
  pending: 'Ожидает',
  running: 'Выполняется',
  completed: 'Завершён',
  failed: 'Ошибка',
}

const backfillStatusColors: Record<BackfillTaskStatus, string> = {
  pending: 'badge-secondary',
  running: 'badge-info',
  completed: 'badge-success',
  failed: 'badge-danger',
}

export default function SensorBackfillSection({
  backfillData,
  isLoadingBackfill,
  startBackfillMutation,
  hasActiveProfile,
}: SensorBackfillSectionProps) {
  return (
    <div className="sensor-backfill-section">
      <div className="section-header">
        <h3>Пересчёт данных</h3>
        <button
          className="btn btn-primary btn-sm"
          onClick={() => {
            if (confirm('Запустить пересчёт physical_value для всех записей по активному профилю?')) {
              startBackfillMutation.mutate()
            }
          }}
          disabled={startBackfillMutation.isPending || !hasActiveProfile}
          title={!hasActiveProfile ? 'Нет активного профиля' : ''}
        >
          {startBackfillMutation.isPending ? 'Запуск...' : 'Запустить пересчёт'}
        </button>
      </div>

      {isLoadingBackfill && <Loading />}

      {!isLoadingBackfill && backfillData && backfillData.backfill_tasks.length > 0 && (
        <div className="backfill-list">
          <table>
            <thead>
              <tr>
                <th>Статус</th>
                <th>Прогресс</th>
                <th>Создан</th>
                <th>Ошибка</th>
              </tr>
            </thead>
            <tbody>
              {backfillData.backfill_tasks.map((task: BackfillTask) => {
                const progress = task.total_records
                  ? Math.round((task.processed_records / task.total_records) * 100)
                  : 0
                return (
                  <tr key={task.id}>
                    <td>
                      <span className={`badge ${backfillStatusColors[task.status]}`}>
                        {backfillStatusLabels[task.status]}
                      </span>
                    </td>
                    <td>
                      {task.total_records !== null ? (
                        <>
                          <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                            <div style={{
                              flex: 1,
                              height: '8px',
                              background: 'var(--border-color, #e0e0e0)',
                              borderRadius: '4px',
                              overflow: 'hidden',
                            }}>
                              <div style={{
                                width: `${progress}%`,
                                height: '100%',
                                background: task.status === 'failed'
                                  ? 'var(--color-danger, #dc3545)'
                                  : 'var(--color-primary, #1976d2)',
                                borderRadius: '4px',
                                transition: 'width 0.3s',
                              }} />
                            </div>
                            <span style={{ fontSize: '0.85em', minWidth: '80px' }}>
                              {task.processed_records} / {task.total_records}
                            </span>
                          </div>
                        </>
                      ) : (
                        <span className="text-muted">—</span>
                      )}
                    </td>
                    <td>
                      {format(new Date(task.created_at), 'dd MMM HH:mm')}
                    </td>
                    <td>
                      {task.error_message && (
                        <span className="text-danger" title={task.error_message}>
                          {task.error_message.length > 50
                            ? task.error_message.slice(0, 50) + '...'
                            : task.error_message}
                        </span>
                      )}
                    </td>
                  </tr>
                )
              })}
            </tbody>
          </table>
        </div>
      )}

      {!isLoadingBackfill && backfillData && backfillData.backfill_tasks.length === 0 && (
        <p className="text-muted">Нет задач пересчёта</p>
      )}
    </div>
  )
}
