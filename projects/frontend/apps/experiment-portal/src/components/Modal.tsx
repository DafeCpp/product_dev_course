import { ReactNode, useEffect, useId, useRef } from 'react'
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

const FOCUSABLE =
    'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])'

function Modal({ isOpen, onClose, title, children, disabled = false, className = '' }: ModalProps) {
    const mouseDownOnOverlay = useRef(false)
    const contentRef = useRef<HTMLDivElement>(null)
    const titleId = useId()

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

    // Move focus into the dialog on open (unless a child autofocused) and give it back on close.
    useEffect(() => {
        if (!isOpen) return
        const previouslyFocused = document.activeElement as HTMLElement | null
        const content = contentRef.current
        if (content && !content.contains(document.activeElement)) {
            content.focus()
        }
        return () => {
            previouslyFocused?.focus?.()
        }
    }, [isOpen])

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

    // Keep Tab / Shift+Tab inside the dialog.
    const handleKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => {
        if (e.key !== 'Tab' || !contentRef.current) return
        const focusable = Array.from(contentRef.current.querySelectorAll<HTMLElement>(FOCUSABLE))
        if (focusable.length === 0) {
            e.preventDefault()
            return
        }
        const first = focusable[0]
        const last = focusable[focusable.length - 1]
        const active = document.activeElement
        if (e.shiftKey && (active === first || active === contentRef.current)) {
            e.preventDefault()
            last.focus()
        } else if (!e.shiftKey && active === last) {
            e.preventDefault()
            first.focus()
        }
    }

    return createPortal(
        <div
            className="modal-overlay"
            onMouseDown={handleOverlayMouseDown}
            onClick={handleOverlayClick}
        >
            <div
                ref={contentRef}
                className={`modal-content ${className}`}
                role="dialog"
                aria-modal="true"
                aria-labelledby={titleId}
                tabIndex={-1}
                onKeyDown={handleKeyDown}
                onClick={(e) => e.stopPropagation()}
            >
                <div className="modal-header">
                    <h2 id={titleId}>{title}</h2>
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
