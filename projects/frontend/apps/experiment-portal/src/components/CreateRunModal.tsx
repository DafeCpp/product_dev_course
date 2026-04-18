import { useState } from 'react'
import { useMutation, useQueryClient } from '@tanstack/react-query'
import { useNavigate } from 'react-router-dom'
import { runsApi } from '../api/client'
import type { RunCreate } from '../types'
import Modal from './Modal'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'
import { createRunSchema, flatFieldErrors } from '../schemas/forms'
import './CreateRunModal.scss'

interface CreateRunModalProps {
    experimentId: string
    isOpen: boolean
    onClose: () => void
}

function CreateRunModal({ experimentId, isOpen, onClose }: CreateRunModalProps) {
    const navigate = useNavigate()
    const queryClient = useQueryClient()
    const [formData, setFormData] = useState<RunCreate>({
        name: '',
        params: {},
        notes: '',
        metadata: {},
        auto_complete_after_minutes: null,
    })
    const [paramsJson, setParamsJson] = useState('{}')
    const [metadataJson, setMetadataJson] = useState('{}')
    const [autoCompleteInput, setAutoCompleteInput] = useState('')
    const [error, setError] = useState<string | null>(null)
    const [fieldErrors, setFieldErrors] = useState<Record<string, string | undefined>>({})

    const createMutation = useMutation({
        mutationFn: (data: RunCreate) => runsApi.create(experimentId, data),
        onSuccess: (run) => {
            queryClient.invalidateQueries({ queryKey: ['runs', experimentId] })
            queryClient.invalidateQueries({ queryKey: ['experiment', experimentId] })
            notifySuccess('Запуск создан')
            onClose()
            navigate(`/runs/${run.id}`)
        },
        onError: (err: any) => {
            const msg = err.response?.data?.error || 'Ошибка создания запуска'
            setError(msg)
            notifyError(msg)
        },
    })

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)
        setFieldErrors({})

        const result = createRunSchema.safeParse({
            name: formData.name,
            notes: formData.notes,
            paramsJson,
            metadataJson,
        })

        if (!result.success) {
            const errors = flatFieldErrors(result.error)
            setFieldErrors(errors)
            const first = Object.values(errors).find(Boolean) ?? 'Проверьте заполнение формы'
            setError(first)
            notifyError(first)
            return
        }

        const { paramsJson: params, metadataJson: metadata, ...rest } = result.data
        const autoCompleteMinutes = autoCompleteInput.trim() !== ''
            ? parseInt(autoCompleteInput, 10)
            : null
        createMutation.mutate({
            ...rest,
            params,
            notes: rest.notes || undefined,
            metadata: Object.keys(metadata).length > 0 ? metadata : undefined,
            auto_complete_after_minutes: autoCompleteMinutes,
        })
    }

    const handleClose = () => {
        if (!createMutation.isPending) {
            setFormData({
                name: '',
                params: {},
                notes: '',
                metadata: {},
                auto_complete_after_minutes: null,
            })
            setParamsJson('{}')
            setMetadataJson('{}')
            setAutoCompleteInput('')
            setError(null)
            setFieldErrors({})
            onClose()
        }
    }

    return (
        <Modal
            isOpen={isOpen}
            onClose={handleClose}
            title="Создать новый запуск"
            disabled={createMutation.isPending}
        >
            <form onSubmit={handleSubmit} className="modal-form">
                {IS_TEST && error && <div className="error">{error}</div>}

                <div className="form-group">
                    <label htmlFor="run_name">
                        Название <span className="required">*</span>
                    </label>
                    <input
                        id="run_name"
                        type="text"
                        value={formData.name}
                        onChange={(e) => setFormData({ ...formData, name: e.target.value })}
                        required
                        placeholder="Например: Запуск #1"
                        disabled={createMutation.isPending}
                    />
                    {fieldErrors.name && (
                        <small className="field-error">{fieldErrors.name}</small>
                    )}
                </div>

                <div className="form-group">
                    <label htmlFor="run_parameters">Параметры (JSON)</label>
                    <textarea
                        id="run_parameters"
                        value={paramsJson}
                        onChange={(e) => setParamsJson(e.target.value)}
                        placeholder='{"key": "value"}'
                        rows={6}
                        disabled={createMutation.isPending}
                    />
                    <small className="form-hint">
                        JSON объект с параметрами запуска. Оставьте пустым или используйте {'{}'} для пустого объекта.
                    </small>
                    {fieldErrors.paramsJson && (
                        <small className="field-error">{fieldErrors.paramsJson}</small>
                    )}
                </div>

                <div className="form-group">
                    <label htmlFor="run_notes">Заметки</label>
                    <textarea
                        id="run_notes"
                        value={formData.notes}
                        onChange={(e) => setFormData({ ...formData, notes: e.target.value })}
                        placeholder="Дополнительная информация о запуске..."
                        rows={3}
                        disabled={createMutation.isPending}
                    />
                </div>

                <div className="form-group">
                    <label htmlFor="run_auto_complete">Автозавершение (минуты)</label>
                    <input
                        id="run_auto_complete"
                        type="number"
                        min="1"
                        max="1440"
                        value={autoCompleteInput}
                        onChange={(e) => setAutoCompleteInput(e.target.value)}
                        placeholder="Нет (без лимита)"
                        disabled={createMutation.isPending}
                    />
                    <small className="form-hint">
                        Через сколько минут после старта run завершится автоматически. Оставьте пустым для отключения.
                    </small>
                </div>

                <div className="form-group">
                    <label htmlFor="run_metadata">Метаданные (JSON)</label>
                    <textarea
                        id="run_metadata"
                        value={metadataJson}
                        onChange={(e) => setMetadataJson(e.target.value)}
                        placeholder='{"key": "value"}'
                        rows={6}
                        disabled={createMutation.isPending}
                    />
                    <small className="form-hint">
                        JSON объект с метаданными. Оставьте пустым или используйте {'{}'} для пустого объекта.
                    </small>
                    {fieldErrors.metadataJson && (
                        <small className="field-error">{fieldErrors.metadataJson}</small>
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
                        {createMutation.isPending ? 'Создание...' : 'Создать запуск'}
                    </button>
                </div>
            </form>
        </Modal>
    )
}

export default CreateRunModal
