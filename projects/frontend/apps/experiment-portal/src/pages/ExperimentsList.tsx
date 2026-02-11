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
  FloatingActionButton,
  MaterialSelect,
  Tags,
  experimentStatusMap,
} from '../components/common'
import CreateExperimentModal from '../components/CreateExperimentModal'
import { setActiveProjectId } from '../utils/activeProject'
import { downloadBlob } from '../utils/download'
import { notifyError, notifySuccess } from '../utils/notify'
import { IS_TEST } from '../utils/env'
import './ExperimentsList.scss'

function ExperimentsList() {
  const [projectId, setProjectId] = useState<string>('')
  const [status, setStatus] = useState<string>('')
  const [searchQuery, setSearchQuery] = useState<string>('')
  const [page, setPage] = useState(1)
  const [isCreateModalOpen, setIsCreateModalOpen] = useState(false)
  const [exporting, setExporting] = useState(false)
  const pageSize = 20

  // Загружаем список проектов для автоматического выбора первого проекта
  const { data: projectsData, isLoading: projectsLoading } = useQuery({
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
  const isBusy = isLoading || projectsLoading
  const loadingMessage =
    projectsLoading && !projectsData
      ? 'Загрузка проектов...'
      : 'Загрузка экспериментов...'

  return (
    <div className="experiments-list">
      {isBusy && !error && <Loading message={loadingMessage} />}
      {IS_TEST && error && (
        <Error
          message={
            error instanceof Error
              ? error.message
              : 'Ошибка загрузки экспериментов. Убедитесь, что выбран проект.'
          }
        />
      )}
      {!IS_TEST && error && (
        <EmptyState message="Не удалось загрузить эксперименты. Проверьте выбранный проект." />
      )}
      {!projectId && projectsData?.projects && projectsData.projects.length === 0 && (
        <EmptyState message="У вас нет проектов. Создайте проект, чтобы начать работу с экспериментами." />
      )}

      {!isBusy && !error && (
        <>
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
                  disabled={isBusy}
                />
              </div>
              <MaterialSelect
                id="experiment_project_id"
                label="Проект"
                value={projectId}
                onChange={(id) => {
                  setProjectId(id)
                  setActiveProjectId(id)
                  setPage(1)
                }}
                disabled={isBusy}
              >
                {projectsData?.projects.map((project) => (
                  <option key={project.id} value={project.id}>
                    {project.name}
                  </option>
                ))}
              </MaterialSelect>
              <MaterialSelect
                id="experiment_status"
                label="Статус"
                value={status}
                onChange={(value) => {
                  setStatus(value)
                  setPage(1)
                }}
                disabled={isBusy}
              >
                <option value="">Все</option>
                <option value="created">Создан</option>
                <option value="running">Выполняется</option>
                <option value="completed">Завершен</option>
                <option value="failed">Ошибка</option>
                <option value="archived">Архивирован</option>
              </MaterialSelect>
              <div className="form-group export-actions">
                <label>Экспорт</label>
                <div className="export-btns">
                  <button
                    type="button"
                    className="btn btn-secondary btn-sm"
                    disabled={exporting || !projectId}
                    onClick={async () => {
                      setExporting(true)
                      try {
                        const csv = await experimentsApi.exportData({
                          project_id: projectId,
                          format: 'csv',
                          status: status || undefined,
                        })
                        downloadBlob(csv, 'experiments.csv', 'text/csv')
                        notifySuccess('CSV экспортирован')
                      } catch {
                        notifyError('Ошибка экспорта CSV')
                      } finally {
                        setExporting(false)
                      }
                    }}
                  >
                    {exporting ? 'Экспорт...' : 'CSV'}
                  </button>
                  <button
                    type="button"
                    className="btn btn-secondary btn-sm"
                    disabled={exporting || !projectId}
                    onClick={async () => {
                      setExporting(true)
                      try {
                        const json = await experimentsApi.exportData({
                          project_id: projectId,
                          format: 'json',
                          status: status || undefined,
                        })
                        downloadBlob(json, 'experiments.json', 'application/json')
                        notifySuccess('JSON экспортирован')
                      } catch {
                        notifyError('Ошибка экспорта JSON')
                      } finally {
                        setExporting(false)
                      }
                    }}
                  >
                    JSON
                  </button>
                </div>
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
        defaultProjectId={projectId}
      />

      {typeof document !== 'undefined' &&
        createPortal(
          <FloatingActionButton
            onClick={() => setIsCreateModalOpen(true)}
            title="Создать эксперимент"
            ariaLabel="Создать эксперимент"
          />,
          document.body
        )}
    </div>
  )
}

export default ExperimentsList

