import { Link } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import { runsApi } from '../api/client'
import { format } from 'date-fns'
import { StatusBadge, Loading, Error, EmptyState, runStatusMap } from './common'
import './RunsList.css'

interface RunsListProps {
  experimentId: string
}

function RunsList({ experimentId }: RunsListProps) {
  const { data, isLoading, error } = useQuery({
    queryKey: ['runs', experimentId],
    queryFn: () => runsApi.list(experimentId),
  })

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
    return <Loading message="Загрузка запусков..." />
  }

  if (error) {
    return <Error message="Ошибка загрузки запусков" />
  }

  if (!data || data.runs.length === 0) {
    return <EmptyState message="Запуски не найдены" />
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
                  <StatusBadge status={run.status} statusMap={runStatusMap} />
                </td>
                <td>
                  <div className="parameters-preview">
                    {Object.keys(run.params).slice(0, 3).map((key) => (
                      <span key={key} className="param-item">
                        {key}: {String(run.params[key]).substring(0, 20)}
                      </span>
                    ))}
                    {Object.keys(run.params).length > 3 && (
                      <span className="param-more">
                        +{Object.keys(run.params).length - 3}
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
                  {run.finished_at
                    ? format(new Date(run.finished_at), 'dd MMM HH:mm')
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

