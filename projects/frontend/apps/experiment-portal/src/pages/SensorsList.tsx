import { useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { Link } from 'react-router-dom'
import { sensorsApi } from '../api/client'
import { format, formatDistanceToNow } from 'date-fns'
import './SensorsList.css'

function SensorsList() {
  const [projectId, setProjectId] = useState<string>('')
  const [page, setPage] = useState(1)
  const pageSize = 20

  const { data, isLoading, error } = useQuery({
    queryKey: ['sensors', projectId, page],
    queryFn: () =>
      sensorsApi.list({
        project_id: projectId || undefined,
        page,
        page_size: pageSize,
      }),
  })

  const getStatusBadge = (status: string) => {
    const badges: Record<string, string> = {
      registering: 'badge-secondary',
      active: 'badge-success',
      inactive: 'badge-warning',
      decommissioned: 'badge-secondary',
    }
    return badges[status] || 'badge-secondary'
  }

  const getStatusText = (status: string) => {
    const texts: Record<string, string> = {
      registering: 'Регистрация',
      active: 'Активен',
      inactive: 'Неактивен',
      decommissioned: 'Выведен из эксплуатации',
    }
    return texts[status] || status
  }

  const getOnlineStatus = (sensor: { last_heartbeat?: string; status: string }) => {
    if (sensor.status !== 'active') {
      return { isOnline: false, label: 'Офлайн', className: 'status-offline' }
    }

    if (!sensor.last_heartbeat) {
      return { isOnline: false, label: 'Нет данных', className: 'status-unknown' }
    }

    const lastHeartbeat = new Date(sensor.last_heartbeat)
    const now = new Date()
    const diffSeconds = (now.getTime() - lastHeartbeat.getTime()) / 1000

    if (diffSeconds < 10) {
      return { isOnline: true, label: 'Онлайн', className: 'status-online' }
    } else if (diffSeconds < 60) {
      return { isOnline: false, label: 'Задержка', className: 'status-delayed' }
    } else {
      return { isOnline: false, label: 'Офлайн', className: 'status-offline' }
    }
  }

  const getLastHeartbeatText = (sensor: { last_heartbeat?: string }) => {
    if (!sensor.last_heartbeat) {
      return 'Никогда'
    }
    const lastHeartbeat = new Date(sensor.last_heartbeat)
    const now = new Date()
    const diffSeconds = (now.getTime() - lastHeartbeat.getTime()) / 1000

    if (diffSeconds < 60) {
      return `${Math.round(diffSeconds)} сек назад`
    }
    return formatDistanceToNow(lastHeartbeat, { addSuffix: true })
  }

  if (isLoading) {
    return <div className="loading">Загрузка...</div>
  }

  if (error) {
    return <div className="error">Ошибка загрузки датчиков</div>
  }

  return (
    <div className="sensors-list">
      <div className="page-header">
        <h2>Монитор датчиков</h2>
        <Link to="/sensors/new" className="btn btn-primary">
          Добавить датчик
        </Link>
      </div>

      <div className="filters card">
        <div className="filters-grid">
          <div className="form-group">
            <label>Project ID</label>
            <input
              type="text"
              placeholder="UUID проекта"
              value={projectId}
              onChange={(e) => {
                setProjectId(e.target.value)
                setPage(1)
              }}
            />
          </div>
        </div>
      </div>

      {data && (
        <>
          <div className="sensors-grid">
            {data.sensors.map((sensor) => {
              const onlineStatus = getOnlineStatus(sensor)
              return (
                <div key={sensor.id} className="sensor-card card">
                  <div className="card-header">
                    <h3 className="card-title">{sensor.name}</h3>
                    <div className="status-group">
                      <span className={`badge ${getStatusBadge(sensor.status)}`}>
                        {getStatusText(sensor.status)}
                      </span>
                      <span className={`status-indicator ${onlineStatus.className}`}>
                        {onlineStatus.label}
                      </span>
                    </div>
                  </div>

                  <div className="sensor-info">
                    <div className="info-row">
                      <strong>Тип:</strong> {sensor.type}
                    </div>
                    <div className="info-row">
                      <strong>Единицы:</strong> {sensor.input_unit} → {sensor.display_unit}
                    </div>
                    {sensor.last_heartbeat && (
                      <div className="info-row">
                        <strong>Последний heartbeat:</strong>{' '}
                        <span className="heartbeat-time">{getLastHeartbeatText(sensor)}</span>
                      </div>
                    )}
                    {sensor.active_profile_id && (
                      <div className="info-row">
                        <strong>Профиль конвертации:</strong> {sensor.active_profile_id.slice(0, 8)}
                        ...
                      </div>
                    )}
                  </div>

                  <div className="sensor-meta">
                    <small>
                      Создан:{' '}
                      {format(new Date(sensor.created_at), 'dd MMM yyyy HH:mm')}
                    </small>
                  </div>
                </div>
              )
            })}
          </div>

          {data.sensors.length === 0 && (
            <div className="empty-state">
              <p>Датчики не найдены</p>
            </div>
          )}

          {data.total > pageSize && (
            <div className="pagination">
              <button
                className="btn btn-secondary"
                onClick={() => setPage((p) => Math.max(1, p - 1))}
                disabled={page === 1}
              >
                Назад
              </button>
              <span>
                Страница {page} из {Math.ceil(data.total / pageSize)}
              </span>
              <button
                className="btn btn-secondary"
                onClick={() =>
                  setPage((p) => Math.min(Math.ceil(data.total / pageSize), p + 1))
                }
                disabled={page >= Math.ceil(data.total / pageSize)}
              >
                Вперед
              </button>
            </div>
          )}
        </>
      )}
    </div>
  )
}

export default SensorsList
