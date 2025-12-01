import { useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { Link } from 'react-router-dom'
import { experimentsApi } from '../api/client'
import { format } from 'date-fns'
import './ExperimentsList.css'

function ExperimentsList() {
  const [projectId, setProjectId] = useState<string>('')
  const [status, setStatus] = useState<string>('')
  const [searchQuery, setSearchQuery] = useState<string>('')
  const [page, setPage] = useState(1)
  const pageSize = 20

  const { data, isLoading, error, refetch } = useQuery({
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

  if (error) {
    return <div className="error">Ошибка загрузки экспериментов</div>
  }

  return (
    <div className="experiments-list">
      <div className="page-header">
        <h2>Эксперименты</h2>
        <Link to="/experiments/new" className="btn btn-primary">
          Создать эксперимент
        </Link>
      </div>

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
                  <span className={`badge ${getStatusBadge(experiment.status)}`}>
                    {getStatusText(experiment.status)}
                  </span>
                </div>

                {experiment.description && (
                  <p className="experiment-description">{experiment.description}</p>
                )}

                {experiment.experiment_type && (
                  <div className="experiment-type">
                    <strong>Тип:</strong> {experiment.experiment_type}
                  </div>
                )}

                {experiment.tags && experiment.tags.length > 0 && (
                  <div className="tags">
                    {experiment.tags.map((tag) => (
                      <span key={tag} className="tag">
                        {tag}
                      </span>
                    ))}
                  </div>
                )}

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
            <div className="empty-state">
              <p>Эксперименты не найдены</p>
            </div>
          )}

          {data.total > pageSize && (
            <div className="pagination">
              <button
                className="btn btn-secondary"
                onClick={() => setPage((p) => Math.max(1, p - 1))}
                disabled={page === 1}
              >
                Назад
              </button>
              <span>
                Страница {page} из {Math.ceil(data.total / pageSize)}
              </span>
              <button
                className="btn btn-secondary"
                onClick={() =>
                  setPage((p) => Math.min(Math.ceil(data.total / pageSize), p + 1))
                }
                disabled={page >= Math.ceil(data.total / pageSize)}
              >
                Вперед
              </button>
            </div>
          )}
        </>
      )}
    </div>
  )
}

export default ExperimentsList

