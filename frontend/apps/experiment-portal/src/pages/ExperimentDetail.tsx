import { useState } from 'react'
import { useParams, Link, useNavigate } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { experimentsApi, runsApi } from '../api/client'
import { format } from 'date-fns'
import RunsList from '../components/RunsList'
import './ExperimentDetail.css'

function ExperimentDetail() {
  const { id } = useParams<{ id: string }>()
  const navigate = useNavigate()
  const queryClient = useQueryClient()
  const [showEditForm, setShowEditForm] = useState(false)

  const { data: experiment, isLoading, error } = useQuery({
    queryKey: ['experiment', id],
    queryFn: () => experimentsApi.get(id!),
    enabled: !!id,
  })

  const deleteMutation = useMutation({
    mutationFn: () => experimentsApi.delete(id!),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['experiments'] })
      navigate('/experiments')
    },
  })

  const getStatusBadge = (status: string) => {
    const badges: Record<string, string> = {
      created: 'badge-secondary',
      running: 'badge-info',
      completed: 'badge-success',
      failed: 'badge-danger',
      archived: 'badge-secondary',
    }
    return badges[status] || 'badge-secondary'
  }

  const getStatusText = (status: string) => {
    const texts: Record<string, string> = {
      created: 'Создан',
      running: 'Выполняется',
      completed: 'Завершен',
      failed: 'Ошибка',
      archived: 'Архивирован',
    }
    return texts[status] || status
  }

  if (isLoading) {
    return <div className="loading">Загрузка...</div>
  }

  if (error || !experiment) {
    return <div className="error">Эксперимент не найден</div>
  }

  return (
    <div className="experiment-detail">
      <div className="experiment-header card">
        <div className="card-header">
          <h2 className="card-title">{experiment.name}</h2>
          <div className="header-actions">
            <span className={`badge ${getStatusBadge(experiment.status)}`}>
              {getStatusText(experiment.status)}
            </span>
            <button
              className="btn btn-secondary"
              onClick={() => setShowEditForm(!showEditForm)}
            >
              Редактировать
            </button>
            <button
              className="btn btn-danger"
              onClick={() => {
                if (confirm('Удалить эксперимент?')) {
                  deleteMutation.mutate()
                }
              }}
            >
              Удалить
            </button>
          </div>
        </div>

        {experiment.description && (
          <div className="description">
            <h3>Описание</h3>
            <p>{experiment.description}</p>
          </div>
        )}

        <div className="experiment-info">
          <div className="info-row">
            <strong>ID:</strong>
            <span className="mono">{experiment.id}</span>
          </div>
          <div className="info-row">
            <strong>Project ID:</strong>
            <span className="mono">{experiment.project_id}</span>
          </div>
          {experiment.experiment_type && (
            <div className="info-row">
              <strong>Тип:</strong>
              <span>{experiment.experiment_type}</span>
            </div>
          )}
          <div className="info-row">
            <strong>Создан:</strong>
            <span>
              {format(new Date(experiment.created_at), 'dd MMM yyyy HH:mm')}
            </span>
          </div>
          <div className="info-row">
            <strong>Обновлен:</strong>
            <span>
              {format(new Date(experiment.updated_at), 'dd MMM yyyy HH:mm')}
            </span>
          </div>
        </div>

        {experiment.tags && experiment.tags.length > 0 && (
          <div className="tags-section">
            <h3>Теги</h3>
            <div className="tags">
              {experiment.tags.map((tag) => (
                <span key={tag} className="tag">
                  {tag}
                </span>
              ))}
            </div>
          </div>
        )}

        {experiment.metadata && Object.keys(experiment.metadata).length > 0 && (
          <div className="metadata-section">
            <h3>Метаданные</h3>
            <pre className="metadata-json">
              {JSON.stringify(experiment.metadata, null, 2)}
            </pre>
          </div>
        )}
      </div>

      <div className="runs-section">
        <div className="section-header">
          <h3>Запуски эксперимента</h3>
          <button
            className="btn btn-primary"
            onClick={() => {
              // TODO: Открыть модальное окно или перейти на страницу создания run
              alert('Функция создания run будет добавлена')
            }}
          >
            Новый запуск
          </button>
        </div>
        <RunsList experimentId={id!} />
      </div>
    </div>
  )
}

export default ExperimentDetail

