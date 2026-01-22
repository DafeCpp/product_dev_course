import { ReactNode, useEffect } from 'react'
import { createPortal } from 'react-dom'
import './Modal.scss'

interface ModalProps {
    isOpen: boolean
    onClose: () => void
    title: string
    children: ReactNode
    disabled?: boolean
    className?: string
}

function Modal({ isOpen, onClose, title, children, disabled = false, className = '' }: ModalProps) {
    if (!isOpen) {
        return null
    }

    useEffect(() => {
        const handleEscape = (event: KeyboardEvent) => {
            if (event.key === 'Escape' && !disabled) {
                onClose()
            }
        }

        document.addEventListener('keydown', handleEscape)
        return () => {
            document.removeEventListener('keydown', handleEscape)
        }
    }, [disabled, onClose])

    const handleOverlayClick = () => {
        if (!disabled) {
            onClose()
        }
    }

    return createPortal(
        <div className="modal-overlay" onClick={handleOverlayClick}>
            <div className={`modal-content ${className}`} onClick={(e) => e.stopPropagation()}>
                <div className="modal-header">
                    <h2>{title}</h2>
                    <button
                        type="button"
                        className="modal-close"
                        onClick={onClose}
                        disabled={disabled}
                    >
                        ×
                    </button>
                </div>
                {children}
            </div>
        </div>,
        document.body,
    )
}

export default Modal

