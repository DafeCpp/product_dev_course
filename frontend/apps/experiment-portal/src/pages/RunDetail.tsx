import { useParams, Link, useNavigate } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { runsApi } from '../api/client'
import { format } from 'date-fns'
import './RunDetail.css'

function RunDetail() {
  const { id } = useParams<{ id: string }>()
  const navigate = useNavigate()
  const queryClient = useQueryClient()

  const { data: run, isLoading, error } = useQuery({
    queryKey: ['run', id],
    queryFn: () => runsApi.get(id!),
    enabled: !!id,
  })

  const completeMutation = useMutation({
    mutationFn: () => runsApi.complete(id!),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['run', id] })
      queryClient.invalidateQueries({ queryKey: ['runs'] })
    },
  })

  const failMutation = useMutation({
    mutationFn: (reason?: string) => runsApi.fail(id!, reason),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['run', id] })
      queryClient.invalidateQueries({ queryKey: ['runs'] })
    },
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

  if (isLoading) {
    return <div className="loading">Загрузка...</div>
  }

  if (error || !run) {
    return <div className="error">Запуск не найден</div>
  }

  return (
    <div className="run-detail">
      <div className="run-header card">
        <div className="card-header">
          <div>
            <h2 className="card-title">{run.name}</h2>
            <Link
              to={`/experiments/${run.experiment_id}`}
              className="experiment-link"
            >
              ← Вернуться к эксперименту
            </Link>
          </div>
          <div className="header-actions">
            <span className={`badge ${getStatusBadge(run.status)}`}>
              {getStatusText(run.status)}
            </span>
            {run.status === 'running' && (
              <>
                <button
                  className="btn btn-success"
                  onClick={() => completeMutation.mutate()}
                  disabled={completeMutation.isPending}
                >
                  Завершить
                </button>
                <button
                  className="btn btn-danger"
                  onClick={() => {
                    const reason = prompt('Причина ошибки:')
                    if (reason !== null) {
                      failMutation.mutate(reason)
                    }
                  }}
                  disabled={failMutation.isPending}
                >
                  Пометить как ошибка
                </button>
              </>
            )}
          </div>
        </div>

        <div className="run-info">
          <div className="info-row">
            <strong>ID:</strong>
            <span className="mono">{run.id}</span>
          </div>
          <div className="info-row">
            <strong>Experiment ID:</strong>
            <span className="mono">{run.experiment_id}</span>
          </div>
          <div className="info-row">
            <strong>Статус:</strong>
            <span>{getStatusText(run.status)}</span>
          </div>
          {run.started_at && (
            <div className="info-row">
              <strong>Начало:</strong>
              <span>
                {format(new Date(run.started_at), 'dd MMM yyyy HH:mm:ss')}
              </span>
            </div>
          )}
          {run.completed_at && (
            <div className="info-row">
              <strong>Завершение:</strong>
              <span>
                {format(new Date(run.completed_at), 'dd MMM yyyy HH:mm:ss')}
              </span>
            </div>
          )}
          {run.duration_seconds && (
            <div className="info-row">
              <strong>Длительность:</strong>
              <span>{formatDuration(run.duration_seconds)}</span>
            </div>
          )}
          <div className="info-row">
            <strong>Создан:</strong>
            <span>
              {format(new Date(run.created_at), 'dd MMM yyyy HH:mm')}
            </span>
          </div>
        </div>

        {run.notes && (
          <div className="notes-section">
            <h3>Заметки</h3>
            <p>{run.notes}</p>
          </div>
        )}

        <div className="parameters-section">
          <h3>Параметры запуска</h3>
          <pre className="parameters-json">
            {JSON.stringify(run.parameters, null, 2)}
          </pre>
        </div>

        {run.metadata && Object.keys(run.metadata).length > 0 && (
          <div className="metadata-section">
            <h3>Метаданные</h3>
            <pre className="metadata-json">
              {JSON.stringify(run.metadata, null, 2)}
            </pre>
          </div>
        )}
      </div>
    </div>
  )
}

export default RunDetail

