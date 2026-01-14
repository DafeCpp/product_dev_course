import { useState } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { sensorsApi, projectsApi } from '../api/client'
import { format } from 'date-fns'
import type { SensorTokenResponse } from '../types'
import TestTelemetryModal from './TestTelemetryModal'
import TelemetryStreamModal from './TelemetryStreamModal'
import Modal from './Modal'
import {
    StatusBadge,
    Loading,
    Error,
    InfoRow,
    sensorStatusMap,
} from './common'
import './SensorDetailModal.css'
import { notifyError, notifySuccess, notifySuccessSticky } from '../utils/notify'

interface SensorDetailModalProps {
    isOpen: boolean
    onClose: () => void
    sensorId: string
}

function SensorDetailModal({ isOpen, onClose, sensorId }: SensorDetailModalProps) {
    const queryClient = useQueryClient()
    const [showToken, setShowToken] = useState(false)
    const [newToken, setNewToken] = useState<string | null>(null)
    const [showTestTelemetryModal, setShowTestTelemetryModal] = useState(false)
    const [showTelemetryStreamModal, setShowTelemetryStreamModal] = useState(false)
    const [showAddProjectModal, setShowAddProjectModal] = useState(false)
    const [selectedProjectId, setSelectedProjectId] = useState<string>('')

    const { data: sensor, isLoading, error } = useQuery({
        queryKey: ['sensor', sensorId],
        queryFn: () => sensorsApi.get(sensorId),
        enabled: isOpen && !!sensorId,
    })

    // Получаем список проектов датчика
    const {
        data: sensorProjectsData,
        isLoading: isLoadingProjects,
    } = useQuery({
        queryKey: ['sensor', sensorId, 'projects'],
        queryFn: () => sensorsApi.getProjects(sensorId),
        enabled: isOpen && !!sensorId,
    })

    // Получаем список всех доступных проектов для добавления
    const { data: allProjectsData } = useQuery({
        queryKey: ['projects'],
        queryFn: () => projectsApi.list(),
        enabled: isOpen,
    })

    const deleteMutation = useMutation({
        mutationFn: () => sensorsApi.delete(sensorId),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['sensors'] })
            notifySuccess('Датчик удалён')
            onClose()
        },
        onError: (err: any) => {
            const msg = err?.response?.data?.error || err?.message || 'Ошибка удаления датчика'
            notifyError(msg)
        },
    })

    const rotateTokenMutation = useMutation({
        mutationFn: () => sensorsApi.rotateToken(sensorId),
        onSuccess: (response: SensorTokenResponse) => {
            setNewToken(response.token)
            setShowToken(true)
            queryClient.invalidateQueries({ queryKey: ['sensor', sensorId] })
            notifySuccess('Токен обновлён')
        },
        onError: (err: any) => {
            const msg = err?.response?.data?.error || err?.message || 'Ошибка ротации токена'
            notifyError(msg)
        },
    })

    // Мутации для управления проектами
    const addProjectMutation = useMutation({
        mutationFn: (projectId: string) => sensorsApi.addProject(sensorId, projectId),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['sensor', sensorId, 'projects'] })
            setShowAddProjectModal(false)
            setSelectedProjectId('')
            notifySuccess('Проект добавлен')
        },
        onError: (err: any) => {
            const msg = err?.response?.data?.error || err?.message || 'Ошибка добавления проекта'
            notifyError(msg)
        },
    })

    const removeProjectMutation = useMutation({
        mutationFn: (projectId: string) => sensorsApi.removeProject(sensorId, projectId),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['sensor', sensorId, 'projects'] })
            notifySuccess('Проект удалён')
        },
        onError: (err: any) => {
            const msg = err?.response?.data?.error || err?.message || 'Ошибка удаления проекта'
            notifyError(msg)
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

    const isPending = deleteMutation.isPending || rotateTokenMutation.isPending || addProjectMutation.isPending || removeProjectMutation.isPending

    return (
        <>
            <Modal
                isOpen={isOpen}
                onClose={onClose}
                title={sensor?.name || 'Датчик'}
                disabled={isPending}
                className="sensor-detail-modal"
            >
                <div className="modal-body">
                    {isLoading && <Loading />}

                    {error && (
                        <Error
                            message={
                                error && typeof error === 'object' && 'message' in error
                                    ? String(error.message)
                                    : 'Ошибка загрузки датчика'
                            }
                        />
                    )}

                    {!isLoading && !error && sensor && (
                        <div className="sensor-header-content">
                            <div className="sensor-header-actions">
                                <StatusBadge status={sensor.status} statusMap={sensorStatusMap} />
                                <button
                                    className="btn btn-primary btn-sm"
                                    onClick={() => setShowTestTelemetryModal(true)}
                                    disabled={isPending}
                                >
                                    Тестовая отправка
                                </button>
                                <button
                                    className="btn btn-secondary btn-sm"
                                    onClick={() => setShowTelemetryStreamModal(true)}
                                    disabled={isPending}
                                >
                                    Live telemetry
                                </button>
                                <button
                                    className="btn btn-secondary btn-sm"
                                    onClick={() => rotateTokenMutation.mutate()}
                                    disabled={isPending}
                                >
                                    {rotateTokenMutation.isPending ? 'Ротация...' : 'Ротация токена'}
                                </button>
                                <button
                                    className="btn btn-danger btn-sm"
                                    onClick={() => {
                                        if (confirm('Удалить датчик? Это действие нельзя отменить.')) {
                                            deleteMutation.mutate()
                                        }
                                    }}
                                    disabled={isPending}
                                >
                                    Удалить
                                </button>
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
                                                notifySuccessSticky(
                                                    'Токен в буфере обмена. Не закрывайте, пока не сохраните.',
                                                    'Токен скопирован'
                                                )
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
                                <InfoRow label="Основной проект" value={<span className="mono">{sensor.project_id}</span>} />
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

                            {/* Секция управления проектами */}
                            <div className="sensor-projects-section">
                                <div className="section-header">
                                    <h3>Проекты датчика</h3>
                                    <button
                                        className="btn btn-primary btn-sm"
                                        onClick={() => setShowAddProjectModal(true)}
                                        disabled={isPending}
                                    >
                                        Добавить проект
                                    </button>
                                </div>

                                {isLoadingProjects && <Loading />}

                                {!isLoadingProjects && sensorProjectsData && (
                                    <>
                                        {sensorProjectsData.project_ids.length === 0 ? (
                                            <p className="text-muted">Датчик не привязан ни к одному проекту</p>
                                        ) : (
                                            <div className="projects-list">
                                                <table>
                                                    <thead>
                                                        <tr>
                                                            <th>ID проекта</th>
                                                            <th>Название</th>
                                                            <th>Действия</th>
                                                        </tr>
                                                    </thead>
                                                    <tbody>
                                                        {sensorProjectsData.project_ids.map((projectId) => {
                                                            const project = allProjectsData?.projects.find(
                                                                (p) => p.id === projectId
                                                            )
                                                            const isPrimary = projectId === sensor.project_id

                                                            return (
                                                                <tr key={projectId}>
                                                                    <td>
                                                                        <span className="mono">{projectId}</span>
                                                                        {isPrimary && (
                                                                            <span className="badge badge-primary" style={{ marginLeft: '8px' }}>
                                                                                Основной
                                                                            </span>
                                                                        )}
                                                                    </td>
                                                                    <td>{project?.name || 'Неизвестный проект'}</td>
                                                                    <td>
                                                                        {!isPrimary && (
                                                                            <button
                                                                                className="btn btn-danger btn-sm"
                                                                                onClick={() => {
                                                                                    if (
                                                                                        confirm(
                                                                                            `Удалить датчик из проекта ${project?.name || projectId}?`
                                                                                        )
                                                                                    ) {
                                                                                        removeProjectMutation.mutate(projectId)
                                                                                    }
                                                                                }}
                                                                                disabled={isPending}
                                                                            >
                                                                                Удалить
                                                                            </button>
                                                                        )}
                                                                        {isPrimary && (
                                                                            <span className="text-muted">Нельзя удалить основной проект</span>
                                                                        )}
                                                                    </td>
                                                                </tr>
                                                            )
                                                        })}
                                                    </tbody>
                                                </table>
                                            </div>
                                        )}
                                    </>
                                )}
                            </div>
                        </div>
                    )}
                </div>
            </Modal>

            {/* Модальное окно для добавления проекта */}
            {showAddProjectModal && (
                <div className="modal-overlay" onClick={() => setShowAddProjectModal(false)}>
                    <div className="modal-content" onClick={(e) => e.stopPropagation()}>
                        <div className="modal-header">
                            <h2>Добавить проект</h2>
                            <button
                                type="button"
                                className="modal-close"
                                onClick={() => {
                                    setShowAddProjectModal(false)
                                    setSelectedProjectId('')
                                }}
                                disabled={isPending}
                            >
                                ×
                            </button>
                        </div>
                        <div className="modal-form">
                            <div className="form-group">
                                <label htmlFor="add-project-select">
                                    Проект <span className="required">*</span>
                                </label>
                                <select
                                    id="add-project-select"
                                    value={selectedProjectId}
                                    onChange={(e) => setSelectedProjectId(e.target.value)}
                                    disabled={isPending}
                                >
                                    <option value="">Выберите проект</option>
                                    {allProjectsData?.projects
                                        .filter(
                                            (p) =>
                                                !sensorProjectsData?.project_ids.includes(p.id)
                                        )
                                        .map((project) => (
                                            <option key={project.id} value={project.id}>
                                                {project.name}
                                            </option>
                                        ))}
                                </select>
                            </div>
                            {allProjectsData?.projects.filter(
                                (p) => !sensorProjectsData?.project_ids.includes(p.id)
                            ).length === 0 && (
                                    <p className="text-muted">
                                        Все доступные проекты уже добавлены к датчику
                                    </p>
                                )}
                            {addProjectMutation.isError && (
                                <div className="error">
                                    {addProjectMutation.error &&
                                        typeof addProjectMutation.error === 'object' &&
                                        'message' in addProjectMutation.error
                                        ? String(addProjectMutation.error.message)
                                        : 'Ошибка при добавлении проекта'}
                                </div>
                            )}
                            <div className="modal-actions">
                                <button
                                    type="button"
                                    className="btn btn-secondary"
                                    onClick={() => {
                                        setShowAddProjectModal(false)
                                        setSelectedProjectId('')
                                    }}
                                    disabled={isPending}
                                >
                                    Отмена
                                </button>
                                <button
                                    type="button"
                                    className="btn btn-primary"
                                    onClick={() => {
                                        if (selectedProjectId) {
                                            addProjectMutation.mutate(selectedProjectId)
                                        }
                                    }}
                                    disabled={!selectedProjectId || isPending}
                                >
                                    {addProjectMutation.isPending ? 'Добавление...' : 'Добавить'}
                                </button>
                            </div>
                        </div>
                    </div>
                </div>
            )}

            {/* Модальное окно для тестовой отправки телеметрии */}
            {sensorId && (
                <>
                    <TestTelemetryModal
                        sensorId={sensorId}
                        sensorToken={newToken || null}
                        isOpen={showTestTelemetryModal}
                        onClose={() => setShowTestTelemetryModal(false)}
                    />
                    <TelemetryStreamModal
                        sensorId={sensorId}
                        isOpen={showTelemetryStreamModal}
                        onClose={() => setShowTelemetryStreamModal(false)}
                    />
                </>
            )}
        </>
    )
}

export default SensorDetailModal

