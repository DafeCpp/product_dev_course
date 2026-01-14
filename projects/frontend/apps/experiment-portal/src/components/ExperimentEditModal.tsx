import { useEffect, useState } from 'react'
import { useMutation, useQueryClient } from '@tanstack/react-query'
import { experimentsApi } from '../api/client'
import type { Experiment, ExperimentUpdate } from '../types'
import Modal from './Modal'
import { experimentStatusMap } from './common/statusMaps'
import { MaterialSelect } from './common'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'
import './CreateRunModal.css'

interface ExperimentEditModalProps {
    isOpen: boolean
    onClose: () => void
    experiment: Experiment
}

function ExperimentEditModal({ isOpen, onClose, experiment }: ExperimentEditModalProps) {
    const queryClient = useQueryClient()

    const [name, setName] = useState('')
    const [description, setDescription] = useState('')
    const [experimentType, setExperimentType] = useState('')
    const [tagsInput, setTagsInput] = useState('')
    const [metadataInput, setMetadataInput] = useState('{}')
    const [status, setStatus] = useState<Experiment['status']>('draft')
    const [error, setError] = useState<string | null>(null)

    useEffect(() => {
        if (!isOpen) return
        setError(null)
        setName(experiment.name ?? '')
        setDescription(experiment.description ?? '')
        setExperimentType(experiment.experiment_type ?? '')
        setTagsInput((experiment.tags || []).join(', '))
        setMetadataInput(JSON.stringify(experiment.metadata || {}, null, 2))
        setStatus(experiment.status)
    }, [experiment, isOpen])

    const updateMutation = useMutation({
        mutationFn: async (data: ExperimentUpdate) => experimentsApi.update(experiment.id, data),
        onSuccess: async () => {
            await queryClient.invalidateQueries({ queryKey: ['experiments'] })
            await queryClient.invalidateQueries({ queryKey: ['experiment', experiment.id] })
            notifySuccess('Эксперимент обновлён')
            onClose()
        },
        onError: (err: any) => {
            const msg =
                err?.response?.data?.error ||
                err?.response?.data?.message ||
                err?.message ||
                'Ошибка обновления эксперимента'
            setError(msg)
            notifyError(msg)
        },
    })

    const archiveMutation = useMutation({
        mutationFn: async () => experimentsApi.archive(experiment.id, { project_id: experiment.project_id }),
        onSuccess: async () => {
            await queryClient.invalidateQueries({ queryKey: ['experiments'] })
            await queryClient.invalidateQueries({ queryKey: ['experiment', experiment.id] })
            notifySuccess('Эксперимент архивирован')
            onClose()
        },
        onError: (err: any) => {
            const msg =
                err?.response?.data?.error ||
                err?.response?.data?.message ||
                err?.message ||
                'Ошибка архивации эксперимента'
            setError(msg)
            notifyError(msg)
        },
    })

    const isBusy = updateMutation.isPending || archiveMutation.isPending

    const statusTransitions: Record<Experiment['status'], Experiment['status'][]> = {
        draft: ['running', 'archived'],
        running: ['succeeded', 'failed'],
        succeeded: ['archived'],
        failed: ['archived'],
        archived: [],
    }

    const statusOptions = [experiment.status, ...statusTransitions[experiment.status]].filter(
        (v, idx, arr) => arr.indexOf(v) === idx
    )

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)

        if (!name.trim()) {
            const msg = 'Название эксперимента обязательно'
            setError(msg)
            notifyError(msg)
            return
        }

        const tags = tagsInput
            .split(',')
            .map((t) => t.trim())
            .filter((t) => t.length > 0)

        let metadata: Record<string, any> | undefined = undefined
        try {
            const parsed = JSON.parse((metadataInput || '').trim() || '{}')
            metadata = parsed && typeof parsed === 'object' ? parsed : {}
        } catch {
            const msg = 'Неверный формат JSON в метаданных'
            setError(msg)
            notifyError(msg)
            return
        }

        const payload: ExperimentUpdate = {
            name: name.trim() || undefined,
            description: description.trim() || undefined,
            experiment_type: experimentType.trim() || undefined,
            tags,
            metadata,
        }

        const nextStatus = status
        if (nextStatus !== experiment.status) {
            if (nextStatus === 'archived') {
                archiveMutation.mutate()
                return
            }
            payload.status = nextStatus
        }

        updateMutation.mutate(payload)
    }

    const handleClose = () => {
        if (isBusy) return
        onClose()
    }

    return (
        <Modal
            isOpen={isOpen}
            onClose={handleClose}
            title="Эксперимент: редактирование"
            disabled={isBusy}
            className="experiment-modal"
        >
            <form onSubmit={handleSubmit} className="modal-form">
                {IS_TEST && error && <div className="error">{error}</div>}

                <div className="form-group">
                    <label>
                        Project ID
                    </label>
                    <div className="mono" style={{ padding: '0.5rem 0' }}>
                        {experiment.project_id}
                    </div>
                </div>

                <div className="form-group">
                    <label htmlFor="experiment_edit_status">Статус</label>
                    <MaterialSelect
                        id="experiment_edit_status"
                        value={status}
                        onChange={(value) => setStatus(value as Experiment['status'])}
                        disabled={isBusy || statusOptions.length <= 1}
                    >
                        {statusOptions.map((s) => (
                            <option key={s} value={s}>
                                {experimentStatusMap[s]?.text ?? s}
                            </option>
                        ))}
                    </MaterialSelect>
                    {statusOptions.length <= 1 ? (
                        <small className="form-hint">Для текущего статуса переходы недоступны.</small>
                    ) : (
                        <small className="form-hint">
                            Доступные переходы зависят от текущего статуса (draft → running/archived → …).
                        </small>
                    )}
                </div>

                <div className="form-group">
                    <label htmlFor="experiment_edit_name">
                        Название <span className="required">*</span>
                    </label>
                    <input
                        id="experiment_edit_name"
                        type="text"
                        value={name}
                        onChange={(e) => setName(e.target.value)}
                        required
                        disabled={isBusy}
                    />
                </div>

                <div className="form-group">
                    <label htmlFor="experiment_edit_description">Описание</label>
                    <textarea
                        id="experiment_edit_description"
                        value={description}
                        onChange={(e) => setDescription(e.target.value)}
                        rows={3}
                        disabled={isBusy}
                    />
                </div>

                <div className="form-group">
                    <label htmlFor="experiment_edit_type">Тип эксперимента</label>
                    <MaterialSelect
                        id="experiment_edit_type"
                        value={experimentType}
                        onChange={setExperimentType}
                        disabled={isBusy}
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
                    <label htmlFor="experiment_edit_tags">Теги</label>
                    <input
                        id="experiment_edit_tags"
                        type="text"
                        value={tagsInput}
                        onChange={(e) => setTagsInput(e.target.value)}
                        placeholder="Через запятую: аэродинамика, крыло, naca"
                        disabled={isBusy}
                    />
                </div>

                <div className="form-group">
                    <label htmlFor="experiment_edit_metadata">Метаданные (JSON)</label>
                    <textarea
                        id="experiment_edit_metadata"
                        value={metadataInput}
                        onChange={(e) => setMetadataInput(e.target.value)}
                        rows={8}
                        disabled={isBusy}
                    />
                </div>

                <div className="modal-actions">
                    <button type="button" className="btn btn-secondary" onClick={handleClose} disabled={isBusy}>
                        Отмена
                    </button>
                    <button type="submit" className="btn btn-primary" disabled={isBusy || !name.trim()}>
                        {isBusy ? 'Сохранение...' : 'Сохранить'}
                    </button>
                </div>
            </form>
        </Modal>
    )
}

export default ExperimentEditModal

