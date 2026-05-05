import { useEffect, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import { useApiMutation } from '../hooks/useApiMutation'
import { experimentsApi, projectsApi } from '../api/client'
import type { Experiment, ExperimentCreate } from '../types'
import { getActiveProjectId, setActiveProjectId } from '../utils/activeProject'
import Modal from './Modal'
import { IS_TEST } from '../utils/env'
import { Loading, MaterialSelect } from './common'
import { createExperimentSchema } from '../schemas/forms'
import { useFormErrors } from '../hooks/useFormErrors'
import './CreateRunModal.scss'

interface CreateExperimentModalProps {
    isOpen: boolean
    onClose: () => void
    defaultProjectId?: string
}

function CreateExperimentModal({ isOpen, onClose, defaultProjectId }: CreateExperimentModalProps) {
    const navigate = useNavigate()
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
    const { error, fieldErrors, clearErrors, validate, setError } = useFormErrors()

    const { data: projectsData, isLoading: projectsLoading } = useQuery({
        queryKey: ['projects'],
        queryFn: () => projectsApi.list(),
    })

    const createMutation = useApiMutation<Experiment, ExperimentCreate>({
        mutationFn: (data) => experimentsApi.create(data),
        invalidateKeys: [['experiments']],
        successMessage: 'Эксперимент создан',
        onSuccess: (experiment) => {
            onClose()
            navigate(`/experiments/${experiment.id}`)
        },
        onError: (err: any) => {
            setError(err?.response?.data?.error || 'Ошибка создания эксперимента')
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
        clearErrors()

        const data = validate(createExperimentSchema, {
            project_id: formData.project_id,
            name: formData.name,
            description: formData.description,
            experiment_type: formData.experiment_type,
            tagsInput,
            metadataInput,
        })
        if (!data) return

        const { tagsInput: tags, metadataInput: metadata, ...rest } = data
        createMutation.mutate({
            ...rest,
            tags,
            metadata,
            description: rest.description || undefined,
            experiment_type: rest.experiment_type || undefined,
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
            clearErrors()
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
                            <MaterialSelect
                                id="experiment_project"
                                value={formData.project_id}
                                onChange={(id) => {
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
                            </MaterialSelect>
                            {projectsData?.projects.length === 0 && (
                                <small className="form-hint">
                                    У вас нет проектов. Перейдите на страницу проектов, чтобы создать проект.
                                </small>
                            )}
                            {fieldErrors.project_id && (
                                <small className="field-error">{fieldErrors.project_id}</small>
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
                    {fieldErrors.name && (
                        <small className="field-error">{fieldErrors.name}</small>
                    )}
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
                    <MaterialSelect
                        id="experiment_type"
                        value={formData.experiment_type ?? ''}
                        onChange={(value) =>
                            setFormData({ ...formData, experiment_type: value })
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
                    </MaterialSelect>
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
                    {fieldErrors.metadataInput && (
                        <small className="field-error">{fieldErrors.metadataInput}</small>
                    )}
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

