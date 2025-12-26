import { Link } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import { runsApi } from '../api/client'
import { format } from 'date-fns'
import StatusBadge from './StatusBadge'
import Loading from './Loading'
import Error from './Error'
import EmptyState from './EmptyState'
import { formatDuration } from '../utils/formatDuration'
import './RunsList.css'

interface RunsListProps {
  experimentId: string
}

function RunsList({ experimentId }: RunsListProps) {
  const { data, isLoading, error } = useQuery({
    queryKey: ['runs', experimentId],
    queryFn: () => runsApi.list(experimentId),
  })

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
                  <StatusBadge status={run.status} variant="run" />
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

