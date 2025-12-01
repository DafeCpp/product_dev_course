import { Link } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import { runsApi } from '../api/client'
import { format } from 'date-fns'
import './RunsList.css'

interface RunsListProps {
  experimentId: string
}

function RunsList({ experimentId }: RunsListProps) {
  const { data, isLoading, error } = useQuery({
    queryKey: ['runs', experimentId],
    queryFn: () => runsApi.list(experimentId),
  })

  const getStatusBadge = (status: string) => {
    const badges: Record<string, string> = {
      created: 'badge-secondary',
      running: 'badge-info',
      completed: 'badge-success',
      failed: 'badge-danger',
    }
    return badges[status] || 'badge-secondary'
  }

  const getStatusText = (status: string) => {
    const texts: Record<string, string> = {
      created: 'Создан',
      running: 'Выполняется',
      completed: 'Завершен',
      failed: 'Ошибка',
    }
    return texts[status] || status
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

  if (isLoading) {
    return <div className="loading">Загрузка запусков...</div>
  }

  if (error) {
    return <div className="error">Ошибка загрузки запусков</div>
  }

  if (!data || data.runs.length === 0) {
    return (
      <div className="empty-state">
        <p>Запуски не найдены</p>
      </div>
    )
  }

  return (
    <div className="runs-list">
      <div className="runs-table">
        <table>
          <thead>
            <tr>
              <th>Название</th>
              <th>Статус</th>
              <th>Параметры</th>
              <th>Начало</th>
              <th>Завершение</th>
              <th>Длительность</th>
              <th>Действия</th>
            </tr>
          </thead>
          <tbody>
            {data.runs.map((run) => (
              <tr key={run.id}>
                <td>
                  <Link to={`/runs/${run.id}`} className="run-link">
                    {run.name}
                  </Link>
                </td>
                <td>
                  <span className={`badge ${getStatusBadge(run.status)}`}>
                    {getStatusText(run.status)}
                  </span>
                </td>
                <td>
                  <div className="parameters-preview">
                    {Object.keys(run.parameters).slice(0, 3).map((key) => (
                      <span key={key} className="param-item">
                        {key}: {String(run.parameters[key]).substring(0, 20)}
                      </span>
                    ))}
                    {Object.keys(run.parameters).length > 3 && (
                      <span className="param-more">
                        +{Object.keys(run.parameters).length - 3}
                      </span>
                    )}
                  </div>
                </td>
                <td>
                  {run.started_at
                    ? format(new Date(run.started_at), 'dd MMM HH:mm')
                    : '-'}
                </td>
                <td>
                  {run.completed_at
                    ? format(new Date(run.completed_at), 'dd MMM HH:mm')
                    : '-'}
                </td>
                <td>{formatDuration(run.duration_seconds)}</td>
                <td>
                  <Link
                    to={`/runs/${run.id}`}
                    className="btn btn-secondary btn-sm"
                  >
                    Подробнее
                  </Link>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  )
}

export default RunsList

