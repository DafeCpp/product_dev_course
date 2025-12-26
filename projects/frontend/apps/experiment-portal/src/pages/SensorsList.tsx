import { useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { Link } from 'react-router-dom'
import { sensorsApi } from '../api/client'
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
import './SensorsList.css'

function SensorsList() {
    const [projectId, setProjectId] = useState<string>('')
    const [status, setStatus] = useState<string>('')
    const [page, setPage] = useState(1)
    const pageSize = 20

    const { data, isLoading, error } = useQuery({
        queryKey: ['sensors', projectId, status, page],
        queryFn: () =>
            sensorsApi.list({
                project_id: projectId || undefined,
                status: status || undefined,
                page,
                page_size: pageSize,
            }),
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

    if (isLoading) {
        return <Loading />
    }

    if (error) {
        return <Error message="Ошибка загрузки датчиков" />
    }

    return (
        <div className="sensors-list">
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
                        <label htmlFor="sensor_project_id">Project ID</label>
                        <input
                            id="sensor_project_id"
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
                            <Link
                                key={sensor.id}
                                to={`/sensors/${sensor.id}`}
                                className="sensor-card card"
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
                            </Link>
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
        </div>
    )
}

export default SensorsList

