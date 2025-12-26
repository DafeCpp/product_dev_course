import { useState } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { experimentsApi } from '../api/client'
import { format } from 'date-fns'
import RunsList from '../components/RunsList'
import CreateRunModal from '../components/CreateRunModal'
import StatusBadge from '../components/StatusBadge'
import Loading from '../components/Loading'
import Error from '../components/Error'
import InfoRow from '../components/InfoRow'
import Tags from '../components/Tags'
import MetadataSection from '../components/MetadataSection'
import SectionHeader from '../components/SectionHeader'
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
            <StatusBadge status={experiment.status} variant="experiment" />
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
          <InfoRow label="ID" value={experiment.id} mono />
          <InfoRow label="Project ID" value={experiment.project_id} mono />
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

        <MetadataSection metadata={experiment.metadata} />
      </div>

      <div className="runs-section">
        <SectionHeader
          title="Запуски эксперимента"
          action={
            <button
              className="btn btn-primary"
              onClick={() => {
                // TODO: Открыть модальное окно или перейти на страницу создания run
                alert('Функция создания run будет добавлена')
              }}
            >
              Новый запуск
            </button>
          }
        />
        <RunsList experimentId={id!} />
      </div>
    </div>
  )
}

export default ExperimentDetail

