import { useState } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { sensorsApi } from '../api/client'
import { format } from 'date-fns'
import type { SensorStatus, SensorTokenResponse } from '../types'
import './SensorDetail.css'

function SensorDetail() {
    const { id } = useParams<{ id: string }>()
    const navigate = useNavigate()
    const queryClient = useQueryClient()
    const [showToken, setShowToken] = useState(false)
    const [newToken, setNewToken] = useState<string | null>(null)

    const { data: sensor, isLoading, error } = useQuery({
        queryKey: ['sensor', id],
        queryFn: () => sensorsApi.get(id!),
        enabled: !!id,
    })

    const deleteMutation = useMutation({
        mutationFn: () => sensorsApi.delete(id!),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['sensors'] })
            navigate('/sensors')
        },
    })

    const rotateTokenMutation = useMutation({
        mutationFn: () => sensorsApi.rotateToken(id!),
        onSuccess: (response: SensorTokenResponse) => {
            setNewToken(response.token)
            setShowToken(true)
            queryClient.invalidateQueries({ queryKey: ['sensor', id] })
        },
    })

    const getStatusBadge = (status: SensorStatus) => {
        const badges: Record<SensorStatus, string> = {
            registering: 'badge-secondary',
            active: 'badge-success',
            inactive: 'badge-warning',
            archived: 'badge-secondary',
        }
        return badges[status] || 'badge-secondary'
    }

    const getStatusText = (status: SensorStatus) => {
        const texts: Record<SensorStatus, string> = {
            registering: 'Регистрация',
            active: 'Активен',
            inactive: 'Неактивен',
            archived: 'Архивирован',
        }
        return texts[status] || status
    }

    const formatLastHeartbeat = (heartbeat?: string | null) => {
        if (!heartbeat) return 'Никогда'
        const date = new Date(heartbeat)
        const now = new Date()
        const diffMs = now.getTime() - date.getTime()
        const diffMins = Math.floor(diffMs / 60000)

        if (diffMins < 1) return 'Только что'
        if (diffMins < 60) return `${diffMins} мин назад`
        if (diffMins < 1440) return `${Math.floor(diffMins / 60)} ч назад`
        return format(date, 'dd MMM yyyy HH:mm:ss')
    }

    if (isLoading) {
        return <div className="loading">Загрузка...</div>
    }

    if (error || !sensor) {
        return <div className="error">Датчик не найден</div>
    }

    return (
        <div className="sensor-detail">
            <div className="sensor-header card">
                <div className="card-header">
                    <h2 className="card-title">{sensor.name}</h2>
                    <div className="header-actions">
                        <span className={`badge ${getStatusBadge(sensor.status)}`}>
                            {getStatusText(sensor.status)}
                        </span>
                        <button
                            className="btn btn-secondary"
                            onClick={() => rotateTokenMutation.mutate()}
                            disabled={rotateTokenMutation.isPending}
                        >
                            {rotateTokenMutation.isPending ? 'Ротация...' : 'Ротация токена'}
                        </button>
                        <button
                            className="btn btn-danger"
                            onClick={() => {
                                if (confirm('Удалить датчик? Это действие нельзя отменить.')) {
                                    deleteMutation.mutate()
                                }
                            }}
                        >
                            Удалить
                        </button>
                    </div>
                </div>

                {showToken && newToken && (
                    <div className="token-alert">
                        <p className="warning">
                            ⚠️ Сохраните новый токен сейчас! Он больше не будет показан.
                        </p>
                        <div className="token-box">
                            <code>{newToken}</code>
                            <button
                                type="button"
                                className="btn btn-secondary btn-sm"
                                onClick={() => {
                                    navigator.clipboard.writeText(newToken)
                                    alert('Токен скопирован в буфер обмена')
                                }}
                            >
                                Копировать
                            </button>
                            <button
                                type="button"
                                className="btn btn-secondary btn-sm"
                                onClick={() => {
                                    setShowToken(false)
                                    setNewToken(null)
                                }}
                            >
                                Закрыть
                            </button>
                        </div>
                    </div>
                )}

                <div className="sensor-info">
                    <div className="info-row">
                        <strong>ID:</strong>
                        <span className="mono">{sensor.id}</span>
                    </div>
                    <div className="info-row">
                        <strong>Project ID:</strong>
                        <span className="mono">{sensor.project_id}</span>
                    </div>
                    <div className="info-row">
                        <strong>Тип:</strong>
                        <span>{sensor.type}</span>
                    </div>
                    <div className="info-row">
                        <strong>Входная единица:</strong>
                        <span>{sensor.input_unit}</span>
                    </div>
                    <div className="info-row">
                        <strong>Единица отображения:</strong>
                        <span>{sensor.display_unit}</span>
                    </div>
                    <div className="info-row">
                        <strong>Статус:</strong>
                        <span>{getStatusText(sensor.status)}</span>
                    </div>
                    {sensor.token_preview && (
                        <div className="info-row">
                            <strong>Токен (превью):</strong>
                            <span className="mono">****{sensor.token_preview}</span>
                        </div>
                    )}
                    <div className="info-row">
                        <strong>Последний heartbeat:</strong>
                        <span>{formatLastHeartbeat(sensor.last_heartbeat)}</span>
                    </div>
                    {sensor.active_profile_id && (
                        <div className="info-row">
                            <strong>Активный профиль преобразования:</strong>
                            <span className="mono">{sensor.active_profile_id}</span>
                        </div>
                    )}
                    <div className="info-row">
                        <strong>Создан:</strong>
                        <span>{format(new Date(sensor.created_at), 'dd MMM yyyy HH:mm')}</span>
                    </div>
                    <div className="info-row">
                        <strong>Обновлен:</strong>
                        <span>{format(new Date(sensor.updated_at), 'dd MMM yyyy HH:mm')}</span>
                    </div>
                </div>

                {sensor.calibration_notes && (
                    <div className="calibration-notes-section">
                        <h3>Заметки по калибровке</h3>
                        <p>{sensor.calibration_notes}</p>
                    </div>
                )}
            </div>
        </div>
    )
}

export default SensorDetail

