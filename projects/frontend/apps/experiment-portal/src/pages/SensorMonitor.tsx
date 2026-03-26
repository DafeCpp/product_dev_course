import { useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { sensorsApi, projectsApi } from '../api/client'
import { format, formatDistanceToNow } from 'date-fns'
import { ru } from 'date-fns/locale'
import type { Sensor, ConnectionStatus } from '../types'
import {
  ConnectionStatusIndicator,
  Loading,
  Error,
  EmptyState,
  MaterialSelect,
} from '../components/common'
import SensorStatusSummaryBar from '../components/SensorStatusSummaryBar'
import './SensorMonitor.scss'

const CONNECTION_STATUS_OPTIONS: { value: string; label: string }[] = [
  { value: '', label: 'Все' },
  { value: 'online', label: 'Онлайн' },
  { value: 'delayed', label: 'Задержка' },
  { value: 'offline', label: 'Оффлайн' },
]

const statusBorderColor: Record<ConnectionStatus, string> = {
  online: '#16a34a',
  delayed: '#d97706',
  offline: '#6b7280',
}

function SensorMonitor() {
  const [projectId, setProjectId] = useState<string>('')
  const [connectionFilter, setConnectionFilter] = useState<string>('')

  const { data: projectsData, isLoading: projectsLoading } = useQuery({
    queryKey: ['projects'],
    queryFn: () => projectsApi.list(),
  })

  const { data: sensorsData, isLoading: sensorsLoading, error } = useQuery({
    queryKey: ['sensors', 'monitor', projectId],
    queryFn: () =>
      sensorsApi.list({
        project_id: projectId || undefined,
        limit: 200,
      }),
    refetchInterval: 15_000,
  })

  const { data: statusSummary } = useQuery({
    queryKey: ['sensors', 'status-summary', projectId],
    queryFn: () => sensorsApi.getStatusSummary(projectId),
    enabled: !!projectId,
    refetchInterval: 15_000,
  })

  const sensors: Sensor[] = sensorsData?.sensors ?? []

  const filteredSensors = connectionFilter
    ? sensors.filter((s) => s.connection_status === connectionFilter)
    : sensors

  const isBusy = (projectsLoading && !projectsData) || sensorsLoading

  const formatHeartbeat = (heartbeat?: string | null): string => {
    if (!heartbeat) return 'Никогда'
    const date = new Date(heartbeat)
    const diffMs = Date.now() - date.getTime()
    if (diffMs < 60_000) return 'Только что'
    return formatDistanceToNow(date, { addSuffix: true, locale: ru })
  }

  return (
    <div className="sensor-monitor">
      {isBusy && <Loading message="Загрузка..." />}
      {!isBusy && error && (
        <Error
          message={error instanceof Error ? error.message : 'Ошибка загрузки датчиков'}
        />
      )}

      {!isBusy && !error && (
        <>
          {statusSummary && <SensorStatusSummaryBar summary={statusSummary} />}

          <div className="card filter-panel">
            <div className="filter-panel__header">
              <div>
                <div className="filter-panel__title">Фильтры монитора</div>
                <p className="filter-panel__subtitle">
                  Выберите проект и отфильтруйте датчики по статусу подключения.
                </p>
              </div>
            </div>
            <div className="filters-grid sensor-monitor__filters-grid">
              <MaterialSelect
                id="monitor_project_id"
                label="Проект"
                value={projectId}
                onChange={(id) => setProjectId(id)}
                disabled={projectsLoading}
              >
                <option value="">Все проекты</option>
                {projectsData?.projects.map((p) => (
                  <option key={p.id} value={p.id}>
                    {p.name}
                  </option>
                ))}
              </MaterialSelect>

              <MaterialSelect
                id="monitor_connection_status"
                label="Подключение"
                value={connectionFilter}
                onChange={setConnectionFilter}
              >
                {CONNECTION_STATUS_OPTIONS.map((opt) => (
                  <option key={opt.value} value={opt.value}>
                    {opt.label}
                  </option>
                ))}
              </MaterialSelect>
            </div>
          </div>

          {filteredSensors.length === 0 && (
            <EmptyState message="Датчики не найдены" />
          )}

          <div className="sensor-monitor__grid">
            {filteredSensors.map((sensor) => {
              const borderColor =
                sensor.connection_status
                  ? statusBorderColor[sensor.connection_status]
                  : 'transparent'

              return (
                <div
                  key={sensor.id}
                  className="sensor-monitor__card card"
                  style={{ borderTopColor: borderColor }}
                >
                  <div className="sensor-monitor__card-header">
                    <span className="meta-chip">{sensor.type}</span>
                    {sensor.connection_status && (
                      <ConnectionStatusIndicator
                        status={sensor.connection_status}
                        showLabel
                      />
                    )}
                  </div>

                  <h3 className="sensor-monitor__card-title">{sensor.name}</h3>

                  <div className="sensor-monitor__card-meta">
                    <div className="info-row">
                      <strong>Единицы</strong>
                      <span>
                        {sensor.input_unit} &rarr; {sensor.display_unit}
                      </span>
                    </div>
                    <div className="info-row">
                      <strong>Heartbeat</strong>
                      <span>{formatHeartbeat(sensor.last_heartbeat)}</span>
                    </div>
                    {sensor.last_heartbeat && (
                      <div className="info-row">
                        <strong>Время</strong>
                        <span className="sensor-monitor__ts">
                          {format(new Date(sensor.last_heartbeat), 'dd MMM HH:mm:ss')}
                        </span>
                      </div>
                    )}
                  </div>
                </div>
              )
            })}
          </div>
        </>
      )}
    </div>
  )
}

export default SensorMonitor
