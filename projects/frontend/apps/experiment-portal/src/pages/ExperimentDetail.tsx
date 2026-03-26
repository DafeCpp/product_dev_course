import { useEffect, useState } from 'react'
import { useParams, useNavigate, Link } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { experimentsApi } from '../api/client'
import { format } from 'date-fns'
import RunsList from '../components/RunsList'
import CreateRunModal from '../components/CreateRunModal'
import ExperimentEditModal from '../components/ExperimentEditModal'
import {
  StatusBadge,
  Loading,
  Error,
  EmptyState,
  InfoRow,
  Tags,
  experimentStatusMap,
} from '../components/common'
import './ExperimentDetail.scss'
import { setActiveProjectId } from '../utils/activeProject'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'

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

  useEffect(() => {
    if (!experiment?.project_id) return
    setActiveProjectId(experiment.project_id)
  }, [experiment?.project_id])

  const deleteMutation = useMutation({
    mutationFn: () => experimentsApi.delete(id!),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['experiments'] })
      notifySuccess('Эксперимент удалён')
      navigate('/experiments')
    },
    onError: (err: any) => {
      const msg =
        err?.response?.data?.error ||
        err?.response?.data?.message ||
        err?.message ||
        'Ошибка удаления эксперимента'
      notifyError(msg)
    },
  })

  if (isLoading) {
    return <Loading />
  }

  if (error || !experiment) {
    return IS_TEST ? (
      <Error message="Эксперимент не найден" />
    ) : (
      <EmptyState message="Эксперимент не найден" />
    )
  }

  const formattedCreatedAt = format(new Date(experiment.created_at), 'dd MMM yyyy HH:mm')
  const formattedUpdatedAt = format(new Date(experiment.updated_at), 'dd MMM yyyy HH:mm')

  return (
    <div className="experiment-detail detail-page">
      <section className="compact-page-header card">
        <div className="compact-page-header__top">
          <div className="compact-page-header__main">
            <div className="compact-page-header__eyebrow">Experiment Detail</div>
            <div className="compact-page-header__title-row">
              <h2 className="compact-page-header__title">{experiment.name}</h2>
              <StatusBadge status={experiment.status} statusMap={experimentStatusMap} />
            </div>
            <p className="compact-page-header__description">
              {experiment.description ||
                'Добавьте описание, чтобы команда быстрее понимала цель эксперимента, контекст и критерии оценки.'}
            </p>
          </div>
          <div className="compact-page-header__actions">
            <StatusBadge status={experiment.status} statusMap={experimentStatusMap} />
            <button className="btn btn-secondary" onClick={() => setShowEditForm(true)}>
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
        <div className="compact-page-header__meta">
          <span className="meta-chip">
            type: {experiment.experiment_type ? `${experiment.experiment_type} mode` : 'General mode'}
          </span>
          <span className="meta-chip">project: {experiment.project_id}</span>
          <span className="meta-chip">updated: {formattedUpdatedAt}</span>
        </div>
      </section>

      <div className="detail-grid experiment-detail__grid">
        <div className="experiment-header card detail-card">
          <div className="detail-section-header">
            <div className="detail-section-header__copy">
              <span className="detail-card__eyebrow">Experiment Record</span>
              <h3 className="detail-card__title">Контекст и идентификаторы</h3>
              <p>Ключевые поля карточки, с которыми работает backend, UI и downstream-процессы.</p>
            </div>
          </div>

          <div className="experiment-info">
            <InfoRow label="ID" value={<span className="mono">{experiment.id}</span>} />
            <InfoRow
              label="Project ID"
              value={<span className="mono">{experiment.project_id}</span>}
            />
            {experiment.experiment_type && <InfoRow label="Тип" value={experiment.experiment_type} />}
            <InfoRow label="Создан" value={formattedCreatedAt} />
            <InfoRow label="Обновлен" value={formattedUpdatedAt} />
          </div>

          {experiment.tags && experiment.tags.length > 0 && (
            <div className="tags-section">
              <span className="detail-card__eyebrow">Labels</span>
              <h3 className="detail-card__title">Теги и быстрый контекст</h3>
              <Tags tags={experiment.tags} />
            </div>
          )}
        </div>

        <div className="detail-stack">
          <div className="card detail-card">
            <span className="detail-card__eyebrow">At A Glance</span>
            <h3 className="detail-card__title">Оперативная сводка</h3>
            <div className="detail-meta-grid">
              <div className="detail-meta-card">
                <span>Статус</span>
                <strong>{experiment.status}</strong>
              </div>
              <div className="detail-meta-card">
                <span>Тегов</span>
                <strong>{experiment.tags?.length || 0}</strong>
              </div>
              <div className="detail-meta-card">
                <span>Создан</span>
                <strong>{formattedCreatedAt}</strong>
              </div>
              <div className="detail-meta-card">
                <span>Обновлен</span>
                <strong>{formattedUpdatedAt}</strong>
              </div>
            </div>
          </div>

          <div className="card detail-card metadata-section">
            <span className="detail-card__eyebrow">Metadata</span>
            <h3 className="detail-card__title">Служебные данные</h3>
            {experiment.metadata && Object.keys(experiment.metadata).length > 0 ? (
              <pre className="detail-code-block metadata-json">
                {JSON.stringify(experiment.metadata, null, 2)}
              </pre>
            ) : (
              <div className="detail-note">
                <p>Метаданные пока не заданы. Этот блок останется полезным для системных флагов и допконтекста.</p>
              </div>
            )}
          </div>
        </div>
      </div>

      <section className="runs-section card detail-card">
        <div className="detail-section-header">
          <div className="detail-section-header__copy">
            <span className="detail-card__eyebrow">Run Ledger</span>
            <h3 className="detail-card__title">Запуски эксперимента</h3>
            <p>Управляйте серией запусков, экспортируйте данные и быстро проваливайтесь в детали.</p>
          </div>
          <div style={{ display: 'flex', gap: '8px' }}>
            <Link
              to={`/experiments/${id}/compare`}
              className="btn btn-secondary"
              style={{ textDecoration: 'none' }}
            >
              Compare Runs
            </Link>
            <button className="btn btn-primary" onClick={() => setShowCreateRunModal(true)}>
              Новый запуск
            </button>
          </div>
        </div>
        <RunsList experimentId={id!} />
      </section>

      {id && (
        <CreateRunModal
          experimentId={id}
          isOpen={showCreateRunModal}
          onClose={() => setShowCreateRunModal(false)}
        />
      )}

      {experiment && (
        <ExperimentEditModal
          isOpen={showEditForm}
          onClose={() => setShowEditForm(false)}
          experiment={experiment}
        />
      )}
    </div>
  )
}

export default ExperimentDetail
