import { useState } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import { useApiMutation } from '../hooks/useApiMutation'
import { sensorsApi, projectsApi, conversionProfilesApi, backfillApi } from '../api/client'
import { format } from 'date-fns'
import type { SensorTokenResponse } from '../types'
import TestTelemetryModal from '../components/TestTelemetryModal'
import TelemetryStreamModal from '../components/TelemetryStreamModal'
import ConversionProfileCreateModal from '../components/ConversionProfileCreateModal'
import {
    StatusBadge,
    Loading,
    Error,
    EmptyState,
    InfoRow,
    sensorStatusMap,
} from '../components/common'
import SensorProjectsSection from './sensor-detail/SensorProjectsSection'
import SensorConversionProfiles from './sensor-detail/SensorConversionProfiles'
import SensorErrorLog from './sensor-detail/SensorErrorLog'
import SensorBackfillSection from './sensor-detail/SensorBackfillSection'
import './SensorDetail.scss'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccessSticky } from '../utils/notify'

function SensorDetail() {
    const { id } = useParams<{ id: string }>()
    const navigate = useNavigate()
    const [showToken, setShowToken] = useState(false)
    const [newToken, setNewToken] = useState<string | null>(null)
    const [showTestTelemetryModal, setShowTestTelemetryModal] = useState(false)
    const [showTelemetryStreamModal, setShowTelemetryStreamModal] = useState(false)
    const [showAddProjectModal, setShowAddProjectModal] = useState(false)
    const [selectedProjectId, setSelectedProjectId] = useState<string>('')
    const [showCreateProfileModal, setShowCreateProfileModal] = useState(false)

    const { data: sensor, isLoading, error } = useQuery({
        queryKey: ['sensor', id],
        queryFn: () => sensorsApi.get(id!),
        enabled: !!id,
    })

    // Получаем список проектов датчика
    const {
        data: sensorProjectsData,
        isLoading: isLoadingProjects,
    } = useQuery({
        queryKey: ['sensor', id, 'projects'],
        queryFn: () => sensorsApi.getProjects(id!),
        enabled: !!id,
    })

    // Получаем список всех доступных проектов для добавления
    const { data: allProjectsData } = useQuery({
        queryKey: ['projects'],
        queryFn: () => projectsApi.list(),
    })

    const deleteMutation = useApiMutation({
        mutationFn: () => sensorsApi.delete(id!),
        invalidateKeys: [['sensors']],
        successMessage: 'Датчик удалён',
        errorFallback: 'Ошибка удаления датчика',
        onSuccess: () => navigate('/sensors'),
    })

    const rotateTokenMutation = useApiMutation<SensorTokenResponse>({
        mutationFn: () => sensorsApi.rotateToken(id!),
        invalidateKeys: [['sensor', id]],
        successMessage: 'Токен обновлён',
        errorFallback: 'Ошибка ротации токена',
        onSuccess: (response) => { setNewToken(response.token); setShowToken(true) },
    })

    // Мутации для управления проектами
    const addProjectMutation = useApiMutation<unknown, string>({
        mutationFn: (projectId: string) => sensorsApi.addProject(id!, projectId),
        invalidateKeys: [['sensor', id, 'projects']],
        successMessage: 'Проект добавлен',
        errorFallback: 'Ошибка добавления проекта',
        onSuccess: () => { setShowAddProjectModal(false); setSelectedProjectId('') },
    })

    const removeProjectMutation = useApiMutation<unknown, string>({
        mutationFn: (projectId: string) => sensorsApi.removeProject(id!, projectId),
        invalidateKeys: [['sensor', id, 'projects']],
        successMessage: 'Проект удалён',
        errorFallback: 'Ошибка удаления проекта',
    })

    // Conversion profiles
    const {
        data: profilesData,
        isLoading: isLoadingProfiles,
    } = useQuery({
        queryKey: ['sensor', id, 'profiles'],
        queryFn: () => conversionProfilesApi.list(id!),
        enabled: !!id,
    })

    const publishProfileMutation = useApiMutation<unknown, string>({
        mutationFn: (profileId: string) => conversionProfilesApi.publish(id!, profileId),
        invalidateKeys: [['sensor', id, 'profiles'], ['sensor', id]],
        successMessage: 'Профиль опубликован',
        errorFallback: 'Ошибка публикации профиля',
    })

    // Backfill
    const {
        data: backfillData,
        isLoading: isLoadingBackfill,
    } = useQuery({
        queryKey: ['sensor', id, 'backfill'],
        queryFn: () => backfillApi.list(id!),
        enabled: !!id,
        refetchInterval: (query) => {
            // Auto-refresh while there are running/pending tasks
            const tasks = query.state.data?.backfill_tasks
            if (tasks?.some(t => t.status === 'pending' || t.status === 'running')) {
                return 3000
            }
            return false
        },
    })

    // Error log
    const [errorLogPage, setErrorLogPage] = useState(0)
    const ERROR_LOG_LIMIT = 25
    const {
        data: errorLogData,
        isLoading: isLoadingErrorLog,
    } = useQuery({
        queryKey: ['sensor', id, 'error-log', errorLogPage],
        queryFn: () => sensorsApi.getErrorLog(id!, { limit: ERROR_LOG_LIMIT, offset: errorLogPage * ERROR_LOG_LIMIT }),
        enabled: !!id,
        refetchInterval: 30_000,
        staleTime: 15_000,
    })

    const startBackfillMutation = useApiMutation({
        mutationFn: () => backfillApi.start(id!),
        invalidateKeys: [['sensor', id, 'backfill']],
        successMessage: 'Задача пересчёта создана',
        errorFallback: 'Ошибка запуска пересчёта',
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
        return IS_TEST ? <Error message="Датчик не найден" /> : <EmptyState message="Датчик не найден" />
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
                            onClick={() => setShowTelemetryStreamModal(true)}
                        >
                            Live telemetry
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
                                onClick={async () => {
                                    let copied = false
                                    if (navigator.clipboard) {
                                        try {
                                            await navigator.clipboard.writeText(newToken)
                                            copied = true
                                        } catch { /* fallback below */ }
                                    }
                                    if (!copied) {
                                        try {
                                            const ta = document.createElement('textarea')
                                            ta.value = newToken
                                            ta.style.position = 'fixed'
                                            ta.style.left = '-9999px'
                                            document.body.appendChild(ta)
                                            ta.select()
                                            document.execCommand('copy')
                                            document.body.removeChild(ta)
                                            copied = true
                                        } catch { /* ignore */ }
                                    }
                                    if (copied) {
                                        notifySuccessSticky(
                                            'Токен в буфере обмена. Не закрывайте, пока не сохраните.',
                                            'Токен скопирован'
                                        )
                                    } else {
                                        notifyError('Не удалось скопировать токен — скопируйте вручную')
                                    }
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

                <SensorProjectsSection
                    sensor={sensor}
                    sensorProjectsData={sensorProjectsData}
                    isLoadingProjects={isLoadingProjects}
                    allProjectsData={allProjectsData}
                    showAddProjectModal={showAddProjectModal}
                    selectedProjectId={selectedProjectId}
                    addProjectMutation={addProjectMutation}
                    removeProjectMutation={removeProjectMutation}
                    setShowAddProjectModal={setShowAddProjectModal}
                    setSelectedProjectId={setSelectedProjectId}
                />

                <SensorConversionProfiles
                    profilesData={profilesData}
                    isLoadingProfiles={isLoadingProfiles}
                    publishProfileMutation={publishProfileMutation}
                    onCreateProfile={() => setShowCreateProfileModal(true)}
                />

                <SensorErrorLog
                    errorLogData={errorLogData}
                    isLoadingErrorLog={isLoadingErrorLog}
                    errorLogPage={errorLogPage}
                    setErrorLogPage={setErrorLogPage}
                    errorLogLimit={ERROR_LOG_LIMIT}
                />

                <SensorBackfillSection
                    backfillData={backfillData}
                    isLoadingBackfill={isLoadingBackfill}
                    startBackfillMutation={startBackfillMutation}
                    hasActiveProfile={!!sensor.active_profile_id}
                />
            </div>

            {id && (
                <>
                    <TestTelemetryModal
                        sensorId={id}
                        sensorToken={newToken || null}
                        isOpen={showTestTelemetryModal}
                        onClose={() => setShowTestTelemetryModal(false)}
                    />
                    <TelemetryStreamModal
                        sensorId={id}
                        isOpen={showTelemetryStreamModal}
                        onClose={() => setShowTelemetryStreamModal(false)}
                    />
                    <ConversionProfileCreateModal
                        sensorId={id}
                        isOpen={showCreateProfileModal}
                        onClose={() => setShowCreateProfileModal(false)}
                    />
                </>
            )}
        </div>
    )
}

export default SensorDetail
