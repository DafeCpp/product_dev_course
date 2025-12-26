import { useState } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { experimentsApi } from '../api/client'
import { format } from 'date-fns'
import RunsList from '../components/RunsList'
import CreateRunModal from '../components/CreateRunModal'
import {
  StatusBadge,
  Loading,
  Error,
  InfoRow,
  Tags,
  experimentStatusMap,
} from '../components/common'
import './ExperimentDetail.css'

function ExperimentDetail() {
  const { id } = useParams<{ id: string }>()
  const navigate = useNavigate()
  const queryClient = useQueryClient()
  const [showEditForm, setShowEditForm] = useState(false)
  const [showCreateRunModal, setShowCreateRunModal] = useState(false)

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

  if (isLoading) {
    return <Loading />
  }

  if (error || !experiment) {
    return <Error message="Эксперимент не найден" />
  }

  return (
    <div className="experiment-detail">
      <div className="experiment-header card">
        <div className="card-header">
          <h2 className="card-title">{experiment.name}</h2>
          <div className="header-actions">
            <StatusBadge status={experiment.status} statusMap={experimentStatusMap} />
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
          <InfoRow label="ID" value={<span className="mono">{experiment.id}</span>} />
          <InfoRow label="Project ID" value={<span className="mono">{experiment.project_id}</span>} />
          {experiment.experiment_type && (
            <InfoRow label="Тип" value={experiment.experiment_type} />
          )}
          <InfoRow
            label="Создан"
            value={format(new Date(experiment.created_at), 'dd MMM yyyy HH:mm')}
          />
          <InfoRow
            label="Обновлен"
            value={format(new Date(experiment.updated_at), 'dd MMM yyyy HH:mm')}
          />
        </div>

        {experiment.tags && experiment.tags.length > 0 && (
          <div className="tags-section">
            <h3>Теги</h3>
            <Tags tags={experiment.tags} />
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
            onClick={() => setShowCreateRunModal(true)}
          >
            Новый запуск
          </button>
        </div>
        <RunsList experimentId={id!} />
      </div>

      {id && (
        <CreateRunModal
          experimentId={id}
          isOpen={showCreateRunModal}
          onClose={() => setShowCreateRunModal(false)}
        />
      )}
    </div>
  )
}

export default ExperimentDetail

