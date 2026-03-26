import { useState } from 'react'
import { createPortal } from 'react-dom'
import { useQuery } from '@tanstack/react-query'
import { useNavigate } from 'react-router-dom'
import { sensorsApi, projectsApi } from '../api/client'
import { format } from 'date-fns'
import type { Sensor } from '../types'
import {
  StatusBadge,
  ConnectionStatusIndicator,
  Loading,
  Error,
  EmptyState,
  Pagination,
  FloatingActionButton,
  MaterialSelect,
  sensorStatusMap,
} from '../components/common'
import SensorDetailModal from '../components/SensorDetailModal'
import SensorStatusSummaryBar from '../components/SensorStatusSummaryBar'
import { setActiveProjectId } from '../utils/activeProject'
import './SensorsList.scss'

function SensorsList() {
  const navigate = useNavigate()
  const [projectId, setProjectId] = useState<string>('')
  const [status, setStatus] = useState<string>('')
  const [page, setPage] = useState(1)
  const [selectedSensorId, setSelectedSensorId] = useState<string | null>(null)
  const pageSize = 20

  const { data: projectsData, isLoading: projectsLoading } = useQuery({
    queryKey: ['projects'],
    queryFn: () => projectsApi.list(),
  })

  const { data, isLoading, error } = useQuery({
    queryKey: ['sensors', projectId, status, page],
    queryFn: () =>
      sensorsApi.list({
        project_id: projectId || undefined,
        status: status || undefined,
        page,
        page_size: pageSize,
      }),
    enabled: true,
  })

  const { data: statusSummary } = useQuery({
    queryKey: ['sensors', 'status-summary', projectId],
    queryFn: () => sensorsApi.getStatusSummary(projectId),
    enabled: !!projectId,
    refetchInterval: 30_000,
  })

  const formatLastHeartbeat = (heartbeat?: string | null) => {
    if (!heartbeat) return 'Никогда'

    const date = new Date(heartbeat)
    const now = new Date()
    const diffMs = now.getTime() - date.getTime()
    const diffMins = Math.floor(diffMs / 60000)

    if (diffMins < 1) return 'Только что'
    if (diffMins < 60) return `${diffMins} мин назад`
    if (diffMins < 1440) return `${Math.floor(diffMins / 60)} ч назад`

    return format(date, 'dd MMM yyyy HH:mm')
  }

  const isBusy = (projectsLoading && !projectsData) || isLoading
  const loadingMessage =
    projectsLoading && !projectsData ? 'Загрузка проектов...' : 'Загрузка датчиков...'

  return (
    <div className="sensors-list">
      {isBusy && <Loading message={loadingMessage} />}
      {!isBusy && error && (
        <Error
          message={
            error instanceof Error
              ? error.message
              : 'Ошибка загрузки датчиков. Убедитесь, что выбран проект.'
          }
        />
      )}

      {!isBusy && !projectId && projectsData?.projects && projectsData.projects.length === 0 && (
        <EmptyState message="У вас нет проектов. Создайте проект, чтобы начать работу с датчиками." />
      )}

      {!isBusy && !error && (projectId || (projectsData?.projects?.length ?? 0) > 0) && (
        <>
          {statusSummary && <SensorStatusSummaryBar summary={statusSummary} />}

          <div className="filters card filter-panel">
            <div className="filter-panel__header">
              <div>
                <div className="filter-panel__title">Fleet Filters</div>
                <p className="filter-panel__subtitle">
                  Переключайтесь между проектами и быстро отсекайте устройства по рабочему статусу.
                </p>
              </div>
            </div>

            <div className="filters-grid sensors-filters-grid">
              <MaterialSelect
                id="sensor_project_id"
                label="Проект"
                value={projectId}
                onChange={(id) => {
                  setProjectId(id)
                  setActiveProjectId(id)
                  setPage(1)
                }}
                disabled={projectsLoading || isLoading}
              >
                <option value="">Все проекты</option>
                {projectsData?.projects.map((project) => (
                  <option key={project.id} value={project.id}>
                    {project.name}
                  </option>
                ))}
              </MaterialSelect>
              <MaterialSelect
                id="sensor_status"
                label="Статус"
                value={status}
                onChange={(value) => {
                  setStatus(value)
                  setPage(1)
                }}
                disabled={isLoading}
              >
                <option value="">Все</option>
                <option value="registering">Регистрация</option>
                <option value="active">Активен</option>
                <option value="inactive">Неактивен</option>
                <option value="archived">Архивирован</option>
              </MaterialSelect>
            </div>
          </div>

          {data && (
            <>
              <div className="sensors-grid">
                {data.sensors.map((sensor: Sensor) => (
                  <div
                    key={sensor.id}
                    className="sensor-card card"
                    onClick={() => setSelectedSensorId(sensor.id)}
                    onKeyDown={(event) => {
                      if (event.key === 'Enter' || event.key === ' ') {
                        event.preventDefault()
                        setSelectedSensorId(sensor.id)
                      }
                    }}
                    role="button"
                    tabIndex={0}
                  >
                    <div className="sensor-card__topline">
                      <span className="meta-chip">{sensor.type}</span>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                        {sensor.connection_status && (
                          <ConnectionStatusIndicator
                            status={sensor.connection_status}
                            showLabel
                          />
                        )}
                        <StatusBadge status={sensor.status} statusMap={sensorStatusMap} />
                      </div>
                    </div>

                    <h3 className="sensor-card__title">{sensor.name}</h3>

                    <div className="sensor-card__heartbeat">
                      <span
                        className={`sensor-card__pulse${sensor.last_heartbeat ? ' is-live' : ''}`}
                        aria-hidden="true"
                      />
                      <span>{formatLastHeartbeat(sensor.last_heartbeat)}</span>
                    </div>

                    <div className="sensor-info">
                      <div className="info-row">
                        <strong>Тип</strong>
                        <span>{sensor.type}</span>
                      </div>
                      <div className="info-row">
                        <strong>Единицы</strong>
                        <span>
                          {sensor.input_unit} → {sensor.display_unit}
                        </span>
                      </div>
                      <div className="info-row">
                        <strong>Heartbeat</strong>
                        <span>{formatLastHeartbeat(sensor.last_heartbeat)}</span>
                      </div>
                      {sensor.token_preview && (
                        <div className="info-row">
                          <strong>Токен</strong>
                          <span className="mono">****{sensor.token_preview}</span>
                        </div>
                      )}
                    </div>

                    <div className="sensor-meta">
                      <div className="sensor-meta__item">
                        <span>Создан</span>
                        <strong>{format(new Date(sensor.created_at), 'dd MMM yyyy HH:mm')}</strong>
                      </div>
                      <span className="sensor-meta__cta">Подробнее</span>
                    </div>
                  </div>
                ))}
              </div>

              {data.sensors.length === 0 && <EmptyState message="Датчики не найдены" />}

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

      {typeof document !== 'undefined' &&
        createPortal(
          <FloatingActionButton
            onClick={() => navigate('/sensors/new')}
            title="Зарегистрировать датчик"
            ariaLabel="Зарегистрировать датчик"
          />,
          document.body
        )}

      {selectedSensorId && (
        <SensorDetailModal
          isOpen={!!selectedSensorId}
          onClose={() => setSelectedSensorId(null)}
          sensorId={selectedSensorId}
        />
      )}
    </div>
  )
}

export default SensorsList
