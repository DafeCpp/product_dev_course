import { useState } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { sensorsApi } from '../api/client'
import { format } from 'date-fns'
import type { SensorTokenResponse } from '../types'
import TestTelemetryModal from '../components/TestTelemetryModal'
import {
    StatusBadge,
    Loading,
    Error,
    InfoRow,
    sensorStatusMap,
} from '../components/common'
import './SensorDetail.css'

function SensorDetail() {
    const { id } = useParams<{ id: string }>()
    const navigate = useNavigate()
    const queryClient = useQueryClient()
    const [showToken, setShowToken] = useState(false)
    const [newToken, setNewToken] = useState<string | null>(null)
    const [showTestTelemetryModal, setShowTestTelemetryModal] = useState(false)

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
        return <Loading />
    }

    if (error || !sensor) {
        return <Error message="Датчик не найден" />
    }

    return (
        <div className="sensor-detail">
            <div className="sensor-header card">
                <div className="card-header">
                    <h2 className="card-title">{sensor.name}</h2>
                    <div className="header-actions">
                        <StatusBadge status={sensor.status} statusMap={sensorStatusMap} />
                        <button
                            className="btn btn-primary"
                            onClick={() => setShowTestTelemetryModal(true)}
                        >
                            Тестовая отправка
                        </button>
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
                    <InfoRow label="ID" value={<span className="mono">{sensor.id}</span>} />
                    <InfoRow label="Project ID" value={<span className="mono">{sensor.project_id}</span>} />
                    <InfoRow label="Тип" value={sensor.type} />
                    <InfoRow label="Входная единица" value={sensor.input_unit} />
                    <InfoRow label="Единица отображения" value={sensor.display_unit} />
                    <InfoRow
                        label="Статус"
                        value={
                            <StatusBadge status={sensor.status} statusMap={sensorStatusMap} />
                        }
                    />
                    {sensor.token_preview && (
                        <InfoRow
                            label="Токен (превью)"
                            value={<span className="mono">****{sensor.token_preview}</span>}
                        />
                    )}
                    <InfoRow
                        label="Последний heartbeat"
                        value={formatLastHeartbeat(sensor.last_heartbeat)}
                    />
                    {sensor.active_profile_id && (
                        <InfoRow
                            label="Активный профиль преобразования"
                            value={<span className="mono">{sensor.active_profile_id}</span>}
                        />
                    )}
                    <InfoRow
                        label="Создан"
                        value={format(new Date(sensor.created_at), 'dd MMM yyyy HH:mm')}
                    />
                    <InfoRow
                        label="Обновлен"
                        value={format(new Date(sensor.updated_at), 'dd MMM yyyy HH:mm')}
                    />
                </div>

                {sensor.calibration_notes && (
                    <div className="calibration-notes-section">
                        <h3>Заметки по калибровке</h3>
                        <p>{sensor.calibration_notes}</p>
                    </div>
                )}
            </div>

            {id && (
                <TestTelemetryModal
                    sensorId={id}
                    sensorToken={newToken || null}
                    isOpen={showTestTelemetryModal}
                    onClose={() => setShowTestTelemetryModal(false)}
                />
            )}
        </div>
    )
}

export default SensorDetail

