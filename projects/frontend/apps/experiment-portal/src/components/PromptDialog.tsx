import { useState } from 'react'
import Modal from './Modal'

interface PromptDialogProps {
    isOpen: boolean
    title: string
    label: string
    submitLabel?: string
    inputType?: 'text' | 'number'
    multiline?: boolean
    /** Returns an error message to show, or null when the value is acceptable. */
    validate?: (value: string) => string | null
    onSubmit: (value: string) => void
    onCancel: () => void
}

/** Accessible replacement for window.prompt(): Enter submits, Escape or "Отмена" cancels. */
export default function PromptDialog({ isOpen, title, onCancel, ...formProps }: PromptDialogProps) {
    return (
        <Modal isOpen={isOpen} onClose={onCancel} title={title}>
            {/* Mounted only while open, so every opening starts from an empty field. */}
            <PromptForm {...formProps} onCancel={onCancel} />
        </Modal>
    )
}

function PromptForm({
    label,
    submitLabel = 'OK',
    inputType = 'text',
    multiline = false,
    validate,
    onSubmit,
    onCancel,
}: Omit<PromptDialogProps, 'isOpen' | 'title'>) {
    const [value, setValue] = useState('')
    const [error, setError] = useState<string | null>(null)

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        const problem = validate?.(value) ?? null
        if (problem) {
            setError(problem)
            return
        }
        onSubmit(value)
    }

    const inputProps = {
        id: 'prompt-dialog-input',
        value,
        autoFocus: true,
        'aria-invalid': error ? true : undefined,
        'aria-describedby': error ? 'prompt-dialog-error' : undefined,
    }

    return (
        <form onSubmit={handleSubmit} className="modal-form">
            <div className="form-group">
                <label htmlFor="prompt-dialog-input">{label}</label>
                {multiline ? (
                    <textarea
                        {...inputProps}
                        rows={3}
                        onChange={(e) => setValue(e.target.value)}
                    />
                ) : (
                    <input
                        {...inputProps}
                        type={inputType}
                        onChange={(e) => setValue(e.target.value)}
                    />
                )}
            </div>
            {error && (
                <div className="error" role="alert" id="prompt-dialog-error">
                    {error}
                </div>
            )}
            <div className="modal-actions">
                <button type="button" className="btn btn-secondary" onClick={onCancel}>
                    Отмена
                </button>
                <button type="submit" className="btn btn-primary">
                    {submitLabel}
                </button>
            </div>
        </form>
    )
}
