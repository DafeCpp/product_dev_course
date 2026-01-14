import { useMemo, useState } from 'react'
import { useMutation, useQueryClient } from '@tanstack/react-query'
import Modal from './Modal'
import { MaterialSelect } from './common'
import { runsApi } from '../api/client'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'

type BulkMode = 'add' | 'remove' | 'set'

interface BulkRunTagsModalProps {
    isOpen: boolean
    onClose: () => void
    experimentId: string
    runIds: string[]
}

function _parseTags(input: string): string[] {
    const parts = input
        .split(',')
        .map((t) => t.trim())
        .filter(Boolean)
    return Array.from(new Set(parts))
}

export default function BulkRunTagsModal({ isOpen, onClose, experimentId, runIds }: BulkRunTagsModalProps) {
    const queryClient = useQueryClient()
    const [mode, setMode] = useState<BulkMode>('add')
    const [tagsText, setTagsText] = useState('')
    const [error, setError] = useState<string | null>(null)

    const tags = useMemo(() => _parseTags(tagsText), [tagsText])

    const mutation = useMutation({
        mutationFn: async () => {
            const payload: { run_ids: string[]; set_tags?: string[]; add_tags?: string[]; remove_tags?: string[] } = {
                run_ids: runIds,
            }
            if (mode === 'set') payload.set_tags = tags
            if (mode === 'add') payload.add_tags = tags
            if (mode === 'remove') payload.remove_tags = tags
            return await runsApi.bulkTags(payload)
        },
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['runs', experimentId] })
            onClose()
            setTagsText('')
            setMode('add')
            setError(null)
            notifySuccess('Теги обновлены')
        },
        onError: (err: any) => {
            const msg = err?.response?.data?.error || err?.message || 'Ошибка bulk tagging'
            setError(msg)
            notifyError(msg)
        },
    })

    const disabled = mutation.isPending || runIds.length === 0

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)
        if (mode !== 'set' && tags.length === 0) {
            const msg = 'Укажи хотя бы один тег (через запятую)'
            setError(msg)
            notifyError(msg)
            return
        }
        mutation.mutate()
    }

    return (
        <Modal isOpen={isOpen} onClose={onClose} title="Bulk tagging" disabled={mutation.isPending}>
            <form onSubmit={handleSubmit} className="modal-form">
                {IS_TEST && error && <div className="error">{error}</div>}

                <div className="form-group">
                    <label>Выбрано запусков</label>
                    <div>{runIds.length}</div>
                </div>

                <div className="form-group">
                    <label htmlFor="bulk_mode">Операция</label>
                    <MaterialSelect
                        id="bulk_mode"
                        value={mode}
                        onChange={(value) => setMode(value as BulkMode)}
                        disabled={mutation.isPending}
                    >
                        <option value="add">Добавить теги</option>
                        <option value="remove">Удалить теги</option>
                        <option value="set">Заменить теги (можно очистить)</option>
                    </MaterialSelect>
                </div>

                <div className="form-group">
                    <label htmlFor="bulk_tags">Теги (через запятую)</label>
                    <input
                        id="bulk_tags"
                        type="text"
                        value={tagsText}
                        onChange={(e) => setTagsText(e.target.value)}
                        placeholder="alpha, beta, exp-42"
                        disabled={mutation.isPending}
                    />
                    {mode === 'set' && (
                        <div className="hint">
                            Оставь поле пустым и нажми “Применить”, чтобы очистить теги у выбранных запусков.
                        </div>
                    )}
                </div>

                <div className="modal-actions">
                    <button type="button" className="btn btn-secondary" onClick={onClose} disabled={mutation.isPending}>
                        Отмена
                    </button>
                    <button type="submit" className="btn btn-primary" disabled={disabled}>
                        {mutation.isPending ? 'Применяю...' : 'Применить'}
                    </button>
                </div>
            </form>
        </Modal>
    )
}

