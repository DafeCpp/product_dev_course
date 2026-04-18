import { ReactNode, useEffect, useRef } from 'react'
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
    const mouseDownOnOverlay = useRef(false)

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

    if (!isOpen) {
        return null
    }

    const handleOverlayMouseDown = (e: React.MouseEvent) => {
        mouseDownOnOverlay.current = e.target === e.currentTarget
    }

    const handleOverlayClick = (e: React.MouseEvent) => {
        if (!disabled && mouseDownOnOverlay.current && e.target === e.currentTarget) {
            onClose()
        }
    }

    return createPortal(
        <div
            className="modal-overlay"
            onMouseDown={handleOverlayMouseDown}
            onClick={handleOverlayClick}
        >
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

