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

  const { data: projectsData, isLoading: projectsLoading } = useQuery({
    queryKey: ['projects'],
    queryFn: () => projectsApi.list(),
  })

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
    enabled: !!projectId,
  })

  const isBusy = isLoading || projectsLoading
  const loadingMessage =
    projectsLoading && !projectsData ? 'Загрузка проектов...' : 'Загрузка экспериментов...'

  const handleExport = async (formatType: 'csv' | 'json') => {
    setExporting(true)

    try {
      const blob = await experimentsApi.exportData({
        project_id: projectId,
        format: formatType,
        status: status || undefined,
      })

      downloadBlob(
        blob,
        `experiments.${formatType}`,
        formatType === 'csv' ? 'text/csv' : 'application/json'
      )
      notifySuccess(`${formatType.toUpperCase()} экспортирован`)
    } catch {
      notifyError(`Ошибка экспорта ${formatType.toUpperCase()}`)
    } finally {
      setExporting(false)
    }
  }

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
          <div className="experiments-filter-row">
            <div className="filter-capsule">
              <div className="filter-capsule__search filter-capsule__search--constrained">
                <svg width="15" height="15" viewBox="0 0 20 20" fill="none"><circle cx="9" cy="9" r="5.5" stroke="currentColor" strokeWidth="1.6"/><path d="m13.5 13.5 3 3" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round"/></svg>
                <input
                  type="text"
                  placeholder="Название, описание..."
                  value={searchQuery}
                  onChange={(e) => { setSearchQuery(e.target.value); setPage(1) }}
                  disabled={isBusy}
                />
              </div>
              <MaterialSelect
                id="experiment_project_id"
                label="Проект"
                value={projectId}
                onChange={(id) => { setProjectId(id); setActiveProjectId(id); setPage(1) }}
                disabled={isBusy}
                variant="pill"
                icon={<svg width="14" height="14" viewBox="0 0 20 20" fill="none"><path d="M2.5 6.5A1.5 1.5 0 0 1 4 5h3.3a1.5 1.5 0 0 1 1.06.44l.94.94a1.5 1.5 0 0 0 1.06.44H16a1.5 1.5 0 0 1 1.5 1.5v6A1.5 1.5 0 0 1 16 15.82H4A1.5 1.5 0 0 1 2.5 14.3V6.5Z" stroke="currentColor" strokeWidth="1.5" strokeLinejoin="round"/></svg>}
              >
                {projectsData?.projects?.map((project) => (
                  <option key={project.id} value={project.id}>{project.name}</option>
                ))}
              </MaterialSelect>
              <MaterialSelect
                id="experiment_status"
                label="Статус"
                value={status}
                onChange={(value) => { setStatus(value); setPage(1) }}
                disabled={isBusy}
                variant="pill"
                icon={<svg width="14" height="14" viewBox="0 0 20 20" fill="none"><path d="M10.5 3h5v5l-6.5 6.5a1.5 1.5 0 0 1-2.1 0l-2.9-2.9a1.5 1.5 0 0 1 0-2.1L10.5 3Z" stroke="currentColor" strokeWidth="1.5" strokeLinejoin="round"/><circle cx="12.5" cy="6.5" r=".9" fill="currentColor"/></svg>}
              >
                <option value="">Все</option>
                <option value="created">Создан</option>
                <option value="running">Выполняется</option>
                <option value="completed">Завершен</option>
                <option value="failed">Ошибка</option>
                <option value="archived">Архивирован</option>
              </MaterialSelect>
            </div>
            <div className="experiments-filter-actions">
              <button type="button" className="btn btn-secondary btn-sm" disabled={exporting || !projectId} onClick={() => handleExport('csv')}>
                {exporting ? 'Экспорт...' : 'CSV'}
              </button>
              <button type="button" className="btn btn-secondary btn-sm" disabled={exporting || !projectId} onClick={() => handleExport('json')}>
                JSON
              </button>
            </div>
          </div>

          {data && (
            <>
              <div className="experiments-grid">
                {data.experiments.map((experiment) => (
                  <Link
                    key={experiment.id}
                    to={`/experiments/${experiment.id}`}
                    className="experiment-card card card-link"
                  >
                    <div className="experiment-card__topline">
                      <span className="meta-chip">
                        {experiment.experiment_type || 'General experiment'}
                      </span>
                      <StatusBadge status={experiment.status} statusMap={experimentStatusMap} />
                    </div>

                    <h3 className="experiment-card__title">{experiment.name}</h3>

                    <p className="experiment-description">
                      {experiment.description ||
                        'Описание пока не добавлено. Откройте карточку, чтобы посмотреть детали конфигурации и запусков.'}
                    </p>

                    <Tags tags={experiment.tags} />

                    <div className="experiment-card__footer">
                      <div className="experiment-card__meta">
                        <span>Создан</span>
                        <strong>{format(new Date(experiment.created_at), 'dd MMM yyyy HH:mm')}</strong>
                      </div>
                      <span className="experiment-card__cta">Открыть</span>
                    </div>
                  </Link>
                ))}
              </div>

              {data.experiments.length === 0 && <EmptyState message="Эксперименты не найдены" />}

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
