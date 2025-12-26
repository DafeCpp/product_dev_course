import { useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { Link } from 'react-router-dom'
import { experimentsApi } from '../api/client'
import { format } from 'date-fns'
import StatusBadge from '../components/StatusBadge'
import Loading from '../components/Loading'
import Error from '../components/Error'
import EmptyState from '../components/EmptyState'
import Pagination from '../components/Pagination'
import Tags from '../components/Tags'
import PageHeader from '../components/PageHeader'
import './ExperimentsList.css'

function ExperimentsList() {
  const [projectId, setProjectId] = useState<string>('')
  const [status, setStatus] = useState<string>('')
  const [searchQuery, setSearchQuery] = useState<string>('')
  const [page, setPage] = useState(1)
  const pageSize = 20

  const { data, isLoading, error } = useQuery({
    queryKey: ['experiments', projectId, status, searchQuery, page],
    queryFn: () => {
      if (searchQuery) {
        return experimentsApi.search({
          q: searchQuery,
          project_id: projectId || undefined,
          page,
          page_size: pageSize,
        })
      }
      return experimentsApi.list({
        project_id: projectId || undefined,
        status: status || undefined,
        page,
        page_size: pageSize,
      })
    },
  })

  if (isLoading) {
    return <Loading />
  }

  if (error) {
    return <Error message="Ошибка загрузки экспериментов" />
  }

  return (
    <div className="experiments-list">
      <PageHeader
        title="Эксперименты"
        action={
          <Link to="/experiments/new" className="btn btn-primary">
            Создать эксперимент
          </Link>
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
            <label>Project ID</label>
            <input
              type="text"
              placeholder="UUID проекта"
              value={projectId}
              onChange={(e) => {
                setProjectId(e.target.value)
                setPage(1)
              }}
            />
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
                  <StatusBadge status={experiment.status} variant="experiment" />
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
            totalPages={Math.ceil(data.total / pageSize)}
            onPageChange={setPage}
          />
        </>
      )}
    </div>
  )
}

export default ExperimentsList

