import { useEffect, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { experimentsApi, projectsApi } from '../api/client'
import type { ExperimentCreate } from '../types'
import { getActiveProjectId, setActiveProjectId } from '../utils/activeProject'
import Modal from './Modal'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'
import { Loading } from './common'
import './CreateRunModal.css'

interface CreateExperimentModalProps {
    isOpen: boolean
    onClose: () => void
    defaultProjectId?: string
}

function CreateExperimentModal({ isOpen, onClose, defaultProjectId }: CreateExperimentModalProps) {
    const navigate = useNavigate()
    const queryClient = useQueryClient()
    const [formData, setFormData] = useState<ExperimentCreate>({
        project_id: defaultProjectId || '',
        name: '',
        description: '',
        experiment_type: '',
        tags: [],
        metadata: {},
    })
    const [tagsInput, setTagsInput] = useState('')
    const [metadataInput, setMetadataInput] = useState('{}')
    const [error, setError] = useState<string | null>(null)

    const { data: projectsData, isLoading: projectsLoading } = useQuery({
        queryKey: ['projects'],
        queryFn: () => projectsApi.list(),
    })

    const createMutation = useMutation({
        mutationFn: (data: ExperimentCreate) => experimentsApi.create(data),
        onSuccess: (experiment) => {
            queryClient.invalidateQueries({ queryKey: ['experiments'] })
            notifySuccess('Эксперимент создан')
            onClose()
            navigate(`/experiments/${experiment.id}`)
        },
        onError: (err: any) => {
            const msg = err.response?.data?.error || 'Ошибка создания эксперимента'
            setError(msg)
            notifyError(msg)
        },
    })

    // Если проект выбран в фильтрах списка, проставляем его в форме при открытии модалки.
    useEffect(() => {
        if (!isOpen) return
        // Пробуем префилл:
        // 1) выбранный проект в списке (defaultProjectId)
        // 2) последний активный проект из localStorage (activeProjectId)
        const candidateProjectId = defaultProjectId || getActiveProjectId()
        if (!candidateProjectId) return

        setFormData((prev) => {
            // Не перетираем выбор пользователя, если он уже выбрал проект в модалке
            if (prev.project_id) return prev
            return { ...prev, project_id: candidateProjectId }
        })
        setActiveProjectId(candidateProjectId)
    }, [isOpen, defaultProjectId])

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)

        if (!formData.project_id) {
            const msg = 'Выберите проект'
            setError(msg)
            notifyError(msg)
            return
        }

        if (!formData.name.trim()) {
            const msg = 'Название эксперимента обязательно'
            setError(msg)
            notifyError(msg)
            return
        }

        // Парсинг тегов
        const tags = tagsInput
            .split(',')
            .map((t) => t.trim())
            .filter((t) => t.length > 0)

        // Парсинг metadata
        let metadata = {}
        try {
            metadata = JSON.parse(metadataInput)
        } catch (e) {
            const msg = 'Неверный формат JSON в метаданных'
            setError(msg)
            notifyError(msg)
            return
        }

        createMutation.mutate({
            ...formData,
            name: formData.name.trim(),
            tags,
            metadata,
            description: formData.description || undefined,
            experiment_type: formData.experiment_type || undefined,
        })
    }

    const handleClose = () => {
        if (!createMutation.isPending) {
            setFormData({
                project_id: defaultProjectId || getActiveProjectId() || '',
                name: '',
                description: '',
                experiment_type: '',
                tags: [],
                metadata: {},
            })
            setTagsInput('')
            setMetadataInput('{}')
            setError(null)
            onClose()
        }
    }

    return (
        <Modal
            isOpen={isOpen}
            onClose={handleClose}
            title="Создать эксперимент"
            disabled={createMutation.isPending}
        >
            <form onSubmit={handleSubmit} className="modal-form">
                {IS_TEST && error && <div className="error">{error}</div>}

                <div className="form-group">
                    <label htmlFor="experiment_project">
                        Проект <span className="required">*</span>
                    </label>
                    {projectsLoading ? (
                        <Loading message="Загрузка проектов..." />
                    ) : (
                        <>
                            <select
                                id="experiment_project"
                                value={formData.project_id}
                                onChange={(e) => {
                                    const id = e.target.value
                                    setFormData({ ...formData, project_id: id })
                                    if (id) setActiveProjectId(id)
                                }}
                                required
                                disabled={createMutation.isPending}
                            >
                                <option value="">Выберите проект</option>
                                {projectsData?.projects.map((project) => (
                                    <option key={project.id} value={project.id}>
                                        {project.name}
                                    </option>
                                ))}
                            </select>
                            {projectsData?.projects.length === 0 && (
                                <small className="form-hint">
                                    У вас нет проектов. Перейдите на страницу проектов, чтобы создать проект.
                                </small>
                            )}
                        </>
                    )}
                </div>

                <div className="form-group">
                    <label htmlFor="experiment_name">
                        Название <span className="required">*</span>
                    </label>
                    <input
                        id="experiment_name"
                        type="text"
                        value={formData.name}
                        onChange={(e) => setFormData({ ...formData, name: e.target.value })}
                        required
                        placeholder="Например: Аэродинамические испытания крыла"
                        disabled={createMutation.isPending}
                    />
                </div>

                <div className="form-group">
                    <label htmlFor="experiment_description">Описание</label>
                    <textarea
                        id="experiment_description"
                        value={formData.description}
                        onChange={(e) =>
                            setFormData({ ...formData, description: e.target.value })
                        }
                        placeholder="Детальное описание эксперимента..."
                        rows={3}
                        disabled={createMutation.isPending}
                    />
                </div>

                <div className="form-group">
                    <label htmlFor="experiment_type">Тип эксперимента</label>
                    <select
                        id="experiment_type"
                        value={formData.experiment_type}
                        onChange={(e) =>
                            setFormData({ ...formData, experiment_type: e.target.value })
                        }
                        disabled={createMutation.isPending}
                    >
                        <option value="">Выберите тип</option>
                        <option value="baseline">Бейзлайн (контроль)</option>
                        <option value="benchmark">Бенчмарк / сравнение</option>
                        <option value="validation">Валидация / проверка</option>
                        <option value="calibration">Калибровка</option>
                        <option value="demo">Демо</option>
                        <option value="other">Другое</option>
                    </select>
                </div>

                <div className="form-group">
                    <label htmlFor="experiment_tags">Теги</label>
                    <input
                        id="experiment_tags"
                        type="text"
                        value={tagsInput}
                        onChange={(e) => setTagsInput(e.target.value)}
                        placeholder="Через запятую: аэродинамика, крыло, naca"
                        disabled={createMutation.isPending}
                    />
                    <small className="form-hint">
                        Введите теги через запятую для удобной фильтрации
                    </small>
                </div>

                <div className="form-group">
                    <label htmlFor="experiment_metadata">Метаданные (JSON)</label>
                    <textarea
                        id="experiment_metadata"
                        value={metadataInput}
                        onChange={(e) => setMetadataInput(e.target.value)}
                        placeholder='{"wind_speed": "30 m/s", "angle_of_attack": "0-15 deg"}'
                        rows={6}
                        disabled={createMutation.isPending}
                    />
                    <small className="form-hint">
                        Дополнительные данные эксперимента в формате JSON
                    </small>
                </div>

                <div className="modal-actions">
                    <button
                        type="button"
                        className="btn btn-secondary"
                        onClick={handleClose}
                        disabled={createMutation.isPending}
                    >
                        Отмена
                    </button>
                    <button
                        type="submit"
                        className="btn btn-primary"
                        disabled={createMutation.isPending}
                    >
                        {createMutation.isPending ? 'Создание...' : 'Создать эксперимент'}
                    </button>
                </div>
            </form>
        </Modal>
    )
}

export default CreateExperimentModal

