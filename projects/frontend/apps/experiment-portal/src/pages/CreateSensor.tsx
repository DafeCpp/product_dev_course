import { useState } from 'react'
import { useNavigate, Link } from 'react-router-dom'
import { useMutation, useQuery } from '@tanstack/react-query'
import { sensorsApi, projectsApi } from '../api/client'
import type { SensorCreate, SensorRegisterResponse } from '../types'
import { setActiveProjectId } from '../utils/activeProject'
import { Error, FormGroup, FormActions, Loading } from '../components/common'
import './CreateSensor.css'

function CreateSensor() {
    const navigate = useNavigate()
    const [formData, setFormData] = useState<SensorCreate>({
        project_id: '',
        name: '',
        type: '',
        input_unit: '',
        display_unit: '',
        calibration_notes: '',
    })
    const [selectedProjectIds, setSelectedProjectIds] = useState<string[]>([])
    const [showToken, setShowToken] = useState(false)
    const [error, setError] = useState<string | null>(null)
    const [additionalProjectsStatus, setAdditionalProjectsStatus] = useState<
        'idle' | 'adding' | 'done' | 'error'
    >('idle')
    const [additionalProjectsError, setAdditionalProjectsError] = useState<string | null>(null)

    const { data: projectsData, isLoading: projectsLoading } = useQuery({
        queryKey: ['projects'],
        queryFn: () => projectsApi.list(),
    })

    const createMutation = useMutation({
        mutationFn: (data: SensorCreate) => sensorsApi.create(data),
        onSuccess: (response: SensorRegisterResponse) => {
            const extraProjectIds = selectedProjectIds.filter(
                (pid) => pid && pid !== response.sensor.project_id
            )

            if (extraProjectIds.length > 0) {
                setAdditionalProjectsStatus('adding')
                setAdditionalProjectsError(null)
                    ; (async () => {
                        try {
                            await Promise.all(
                                extraProjectIds.map((pid) => sensorsApi.addProject(response.sensor.id, pid))
                            )
                            setAdditionalProjectsStatus('done')
                        } catch (e: any) {
                            setAdditionalProjectsStatus('error')
                            setAdditionalProjectsError(
                                e?.response?.data?.error ||
                                e?.message ||
                                'Не удалось добавить датчик в дополнительные проекты'
                            )
                        }
                    })()
            } else {
                setAdditionalProjectsStatus('idle')
                setAdditionalProjectsError(null)
            }

            // Показываем токен пользователю
            setShowToken(true)
            // Можно сохранить токен в state для отображения
            setTimeout(() => {
                navigate(`/sensors/${response.sensor.id}`)
            }, 3000) // Через 3 секунды переходим на страницу датчика
        },
        onError: (err: any) => {
            setError(err.response?.data?.error || 'Ошибка регистрации датчика')
        },
    })

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)

        // На всякий случай: если пользователь не выбрал проект через multi-select
        if (!formData.project_id) return

        createMutation.mutate({
            ...formData,
            calibration_notes: formData.calibration_notes || undefined,
        })
    }

    const handleCopyToken = () => {
        if (createMutation.data?.token) {
            navigator.clipboard.writeText(createMutation.data.token)
            alert('Токен скопирован в буфер обмена')
        }
    }

    if (showToken && createMutation.data) {
        return (
            <div className="create-sensor">
                <div className="token-display card">
                    <h2>Датчик успешно зарегистрирован!</h2>
                    <p className="warning">
                        ⚠️ Сохраните токен сейчас! Он больше не будет показан.
                    </p>
                    {additionalProjectsStatus === 'adding' && (
                        <p style={{ marginTop: '0.5rem', color: '#666' }}>
                            Добавляем датчик в дополнительные проекты...
                        </p>
                    )}
                    {additionalProjectsStatus === 'error' && additionalProjectsError && (
                        <div style={{ marginTop: '0.5rem' }}>
                            <Error message={additionalProjectsError} />
                        </div>
                    )}
                    <div className="token-box">
                        <label>Токен датчика:</label>
                        <div className="token-value">
                            <code>{createMutation.data.token}</code>
                            <button
                                type="button"
                                className="btn btn-secondary btn-sm"
                                onClick={handleCopyToken}
                            >
                                Копировать
                            </button>
                        </div>
                    </div>
                    <p className="redirect-info">
                        Через несколько секунд вы будете перенаправлены на страницу датчика...
                    </p>
                    <button
                        type="button"
                        className="btn btn-primary"
                        onClick={() => navigate(`/sensors/${createMutation.data.sensor.id}`)}
                    >
                        Перейти к датчику
                    </button>
                </div>
            </div>
        )
    }

    return (
        <div className="create-sensor">
            <h2>Зарегистрировать датчик</h2>

            {error && <Error message={error} />}

            <form onSubmit={handleSubmit} className="sensor-form card">
                <FormGroup label="Проекты" htmlFor="project_id" required>
                    {projectsLoading ? (
                        <Loading />
                    ) : (
                        <>
                            <select
                                id="project_id"
                                multiple
                                value={selectedProjectIds}
                                onChange={(e) => {
                                    const ids = Array.from(e.target.selectedOptions)
                                        .map((o) => o.value)
                                        .filter(Boolean)

                                    setSelectedProjectIds(ids)

                                    const primary = ids[0] || ''
                                    setFormData({ ...formData, project_id: primary })
                                    if (primary) setActiveProjectId(primary)
                                }}
                                required
                            >
                                {projectsData?.projects.map((project) => (
                                    <option key={project.id} value={project.id}>
                                        {project.name}
                                    </option>
                                ))}
                            </select>
                            <small className="form-hint">
                                Можно выбрать несколько проектов. Первый выбранный проект будет основным.
                            </small>
                            {projectsData?.projects.length === 0 && (
                                <div style={{ marginTop: '0.5rem', fontSize: '0.875rem', color: '#666' }}>
                                    У вас нет проектов.{' '}
                                    <Link to="/projects" style={{ color: 'var(--primary-color, #007bff)' }}>
                                        Перейти к проектам
                                    </Link>
                                </div>
                            )}
                        </>
                    )}
                </FormGroup>

                <FormGroup label="Название" htmlFor="sensor_name" required>
                    <input
                        id="sensor_name"
                        type="text"
                        value={formData.name}
                        onChange={(e) => setFormData({ ...formData, name: e.target.value })}
                        required
                        placeholder="Например: Датчик температуры #1"
                    />
                </FormGroup>

                <FormGroup label="Тип датчика" htmlFor="sensor_type" required>
                    <select
                        id="sensor_type"
                        value={formData.type}
                        onChange={(e) => setFormData({ ...formData, type: e.target.value })}
                        required
                    >
                        <option value="">Выберите тип</option>
                        <option value="temperature">Температура</option>
                        <option value="pressure">Давление</option>
                        <option value="vibration">Вибрация</option>
                        <option value="current">Ток</option>
                        <option value="voltage">Напряжение</option>
                        <option value="humidity">Влажность</option>
                        <option value="other">Другое</option>
                    </select>
                </FormGroup>

                <FormGroup
                    label="Входная единица измерения"
                    htmlFor="input_unit"
                    required
                    hint="Единица измерения сырых данных от датчика"
                >
                    <input
                        id="input_unit"
                        type="text"
                        value={formData.input_unit}
                        onChange={(e) =>
                            setFormData({ ...formData, input_unit: e.target.value })
                        }
                        required
                        placeholder="Например: V (вольты), mV, ADC"
                    />
                </FormGroup>

                <FormGroup
                    label="Единица отображения"
                    htmlFor="display_unit"
                    required
                    hint="Единица измерения для отображения в интерфейсе"
                >
                    <input
                        id="display_unit"
                        type="text"
                        value={formData.display_unit}
                        onChange={(e) =>
                            setFormData({ ...formData, display_unit: e.target.value })
                        }
                        required
                        placeholder="Например: °C, Pa, Hz"
                    />
                </FormGroup>

                <FormGroup label="Заметки по калибровке" htmlFor="calibration_notes">
                    <textarea
                        id="calibration_notes"
                        value={formData.calibration_notes}
                        onChange={(e) =>
                            setFormData({ ...formData, calibration_notes: e.target.value })
                        }
                        placeholder="Дополнительная информация о калибровке датчика..."
                        rows={3}
                    />
                </FormGroup>

                <FormActions>
                    <button
                        type="button"
                        className="btn btn-secondary"
                        onClick={() => navigate('/sensors')}
                    >
                        Отмена
                    </button>
                    <button
                        type="submit"
                        className="btn btn-primary"
                        disabled={createMutation.isPending}
                    >
                        {createMutation.isPending ? 'Регистрация...' : 'Зарегистрировать датчик'}
                    </button>
                </FormActions>
            </form>
        </div>
    )
}

export default CreateSensor

