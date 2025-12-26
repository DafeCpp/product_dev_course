import { useState, useEffect } from 'react'
import { createPortal } from 'react-dom'
import { useQuery } from '@tanstack/react-query'
import { Link, useNavigate } from 'react-router-dom'
import { sensorsApi, projectsApi } from '../api/client'
import { format } from 'date-fns'
import type { Sensor } from '../types'
import {
    StatusBadge,
    Loading,
    Error,
    EmptyState,
    Pagination,
    PageHeader,
    sensorStatusMap,
} from '../components/common'
import SensorDetailModal from '../components/SensorDetailModal'
import './SensorsList.css'

function SensorsList() {
    const navigate = useNavigate()
    const [projectId, setProjectId] = useState<string>('')
    const [status, setStatus] = useState<string>('')
    const [page, setPage] = useState(1)
    const pageSize = 20
    const [selectedSensorId, setSelectedSensorId] = useState<string | null>(null)

    // Загружаем список проектов для автоматического выбора первого проекта
    const { data: projectsData } = useQuery({
        queryKey: ['projects'],
        queryFn: () => projectsApi.list(),
    })

    // Автоматически выбираем первый проект, если project_id не указан
    useEffect(() => {
        if (!projectId && projectsData?.projects && projectsData.projects.length > 0) {
            setProjectId(projectsData.projects[0].id)
        }
    }, [projectId, projectsData])

    const { data, isLoading, error } = useQuery({
        queryKey: ['sensors', projectId, status, page],
        queryFn: () =>
            sensorsApi.list({
                project_id: projectId || undefined,
                status: status || undefined,
                page,
                page_size: pageSize,
            }),
        enabled: !!projectId, // Запрос выполняется только если project_id выбран
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

    return (
        <div className="sensors-list">
            {isLoading && <Loading />}
            {error && (
                <Error
                    message={
                        error instanceof Error
                            ? error.message
                            : 'Ошибка загрузки датчиков. Убедитесь, что выбран проект.'
                    }
                />
            )}

            {!projectId && projectsData?.projects && projectsData.projects.length === 0 && (
                <EmptyState message="У вас нет проектов. Создайте проект, чтобы начать работу с датчиками." />
            )}

            {!isLoading && !error && projectId && (
                <>
                    <PageHeader
                        title="Датчики"
                        action={
                            <Link to="/sensors/new" className="btn btn-primary">
                                Зарегистрировать датчик
                            </Link>
                        }
                    />

                    <div className="filters card">
                        <div className="filters-grid">
                            <div className="form-group">
                                <label htmlFor="sensor_project_id">Проект</label>
                                <select
                                    id="sensor_project_id"
                                    value={projectId}
                                    onChange={(e) => {
                                        setProjectId(e.target.value)
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
                                <label htmlFor="sensor_status">Статус</label>
                                <select
                                    id="sensor_status"
                                    value={status}
                                    onChange={(e) => {
                                        setStatus(e.target.value)
                                        setPage(1)
                                    }}
                                >
                                    <option value="">Все</option>
                                    <option value="registering">Регистрация</option>
                                    <option value="active">Активен</option>
                                    <option value="inactive">Неактивен</option>
                                    <option value="archived">Архивирован</option>
                                </select>
                            </div>
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
                                        style={{ cursor: 'pointer' }}
                                    >
                                        <div className="card-header">
                                            <h3 className="card-title">{sensor.name}</h3>
                                            <StatusBadge status={sensor.status} statusMap={sensorStatusMap} />
                                        </div>

                                        <div className="sensor-info">
                                            <div className="info-row">
                                                <strong>Тип:</strong>
                                                <span>{sensor.type}</span>
                                            </div>
                                            <div className="info-row">
                                                <strong>Единицы:</strong>
                                                <span>
                                                    {sensor.input_unit} → {sensor.display_unit}
                                                </span>
                                            </div>
                                            <div className="info-row">
                                                <strong>Последний heartbeat:</strong>
                                                <span>{formatLastHeartbeat(sensor.last_heartbeat)}</span>
                                            </div>
                                            {sensor.token_preview && (
                                                <div className="info-row">
                                                    <strong>Токен:</strong>
                                                    <span className="mono">****{sensor.token_preview}</span>
                                                </div>
                                            )}
                                        </div>

                                        <div className="sensor-meta">
                                            <small>
                                                Создан:{' '}
                                                {format(new Date(sensor.created_at), 'dd MMM yyyy HH:mm')}
                                            </small>
                                        </div>
                                    </div>
                                ))}
                            </div>

                            {data.sensors.length === 0 && (
                                <EmptyState message="Датчики не найдены" />
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

            {typeof document !== 'undefined' &&
                createPortal(
                    <button
                        className="fab"
                        onClick={() => navigate('/sensors/new')}
                        title="Зарегистрировать датчик"
                        aria-label="Зарегистрировать датчик"
                    >
                        +
                    </button>,
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

