import { useState, useEffect } from 'react'
import { createPortal } from 'react-dom'
import { useQuery } from '@tanstack/react-query'
import { Link } from 'react-router-dom'
import { experimentsApi, projectsApi } from '../api/client'
import { format } from 'date-fns'
import {
  StatusBadge,
  Loading,
  Error,
  EmptyState,
  Pagination,
  PageHeader,
  Tags,
  experimentStatusMap,
} from '../components/common'
import CreateExperimentModal from '../components/CreateExperimentModal'
import { setActiveProjectId } from '../utils/activeProject'
import './ExperimentsList.css'

function ExperimentsList() {
  const [projectId, setProjectId] = useState<string>('')
  const [status, setStatus] = useState<string>('')
  const [searchQuery, setSearchQuery] = useState<string>('')
  const [page, setPage] = useState(1)
  const [isCreateModalOpen, setIsCreateModalOpen] = useState(false)
  const pageSize = 20

  // Загружаем список проектов для автоматического выбора первого проекта
  const { data: projectsData } = useQuery({
    queryKey: ['projects'],
    queryFn: () => projectsApi.list(),
  })

  // Автоматически выбираем первый проект, если project_id не указан
  useEffect(() => {
    if (!projectId && projectsData?.projects && projectsData.projects.length > 0) {
      const id = projectsData.projects[0].id
      setProjectId(id)
      setActiveProjectId(id)
    }
  }, [projectId, projectsData])

  const { data, isLoading, error } = useQuery({
    queryKey: ['experiments', projectId, status, searchQuery, page],
    queryFn: () => {
      if (searchQuery) {
        return experimentsApi.search({
          q: searchQuery,
          project_id: projectId,
          page,
          page_size: pageSize,
        })
      }
      return experimentsApi.list({
        project_id: projectId,
        status: status || undefined,
        page,
        page_size: pageSize,
      })
    },
    enabled: !!projectId, // Запрос выполняется только если project_id выбран
  })

  return (
    <div className="experiments-list">
      {isLoading && <Loading />}
      {error && (
        <Error
          message={
            error instanceof Error
              ? error.message
              : 'Ошибка загрузки экспериментов. Убедитесь, что выбран проект.'
          }
        />
      )}
      {!projectId && projectsData?.projects && projectsData.projects.length === 0 && (
        <EmptyState message="У вас нет проектов. Создайте проект, чтобы начать работу с экспериментами." />
      )}

      {!isLoading && !error && (
        <>
          <PageHeader
            title="Эксперименты"
            action={
              <button
                className="btn btn-primary"
                onClick={() => setIsCreateModalOpen(true)}
              >
                Создать эксперимент
              </button>
            }
          />

          <div className="filters card">
            <div className="filters-grid">
              <div className="form-group">
                <label>Поиск</label>
                <input
                  type="text"
                  placeholder="Название, описание..."
                  value={searchQuery}
                  onChange={(e) => {
                    setSearchQuery(e.target.value)
                    setPage(1)
                  }}
                />
              </div>
              <div className="form-group">
                <label>Проект</label>
                <select
                  value={projectId}
                  onChange={(e) => {
                    const id = e.target.value
                    setProjectId(id)
                    setActiveProjectId(id)
                    setPage(1)
                  }}
                >
                  <option value="">Выберите проект</option>
                  {projectsData?.projects.map((project) => (
                    <option key={project.id} value={project.id}>
                      {project.name}
                    </option>
                  ))}
                </select>
              </div>
              <div className="form-group">
                <label>Статус</label>
                <select
                  value={status}
                  onChange={(e) => {
                    setStatus(e.target.value)
                    setPage(1)
                  }}
                >
                  <option value="">Все</option>
                  <option value="created">Создан</option>
                  <option value="running">Выполняется</option>
                  <option value="completed">Завершен</option>
                  <option value="failed">Ошибка</option>
                  <option value="archived">Архивирован</option>
                </select>
              </div>
            </div>
          </div>

          {data && (
            <>
              <div className="experiments-grid">
                {data.experiments.map((experiment) => (
                  <Link
                    key={experiment.id}
                    to={`/experiments/${experiment.id}`}
                    className="experiment-card card"
                  >
                    <div className="card-header">
                      <h3 className="card-title">{experiment.name}</h3>
                      <StatusBadge status={experiment.status} statusMap={experimentStatusMap} />
                    </div>

                    {experiment.description && (
                      <p className="experiment-description">{experiment.description}</p>
                    )}

                    {experiment.experiment_type && (
                      <div className="experiment-type">
                        <strong>Тип:</strong> {experiment.experiment_type}
                      </div>
                    )}

                    <Tags tags={experiment.tags} />

                    <div className="experiment-meta">
                      <small>
                        Создан:{' '}
                        {format(new Date(experiment.created_at), 'dd MMM yyyy HH:mm')}
                      </small>
                    </div>
                  </Link>
                ))}
              </div>

              {data.experiments.length === 0 && (
                <EmptyState message="Эксперименты не найдены" />
              )}

              <Pagination
                currentPage={page}
                totalItems={data.total}
                pageSize={pageSize}
                onPageChange={setPage}
              />
            </>
          )}
        </>
      )}

      <CreateExperimentModal
        isOpen={isCreateModalOpen}
        onClose={() => setIsCreateModalOpen(false)}
      />

      {typeof document !== 'undefined' &&
        createPortal(
          <button
            className="fab"
            onClick={() => setIsCreateModalOpen(true)}
            title="Создать эксперимент"
            aria-label="Создать эксперимент"
          >
            +
          </button>,
          document.body
        )}
    </div>
  )
}

export default ExperimentsList

