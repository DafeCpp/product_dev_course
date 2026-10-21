import { ReactNode, useEffect, useId, useRef, useState } from 'react'
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

// Give focus back to the element that opened the dialog. A trigger that is disabled while the
// submitted action runs cannot hold focus, so it gets focus back once it is enabled again,
// unless the user has moved focus somewhere else meanwhile. The wait is capped so a trigger
// that stays disabled does not keep an observer alive.
const RESTORE_FOCUS_WAIT_MS = 30_000

function restoreFocus(target: HTMLElement | null) {
    if (!target?.isConnected) return
    const isDisabled = () => (target as HTMLButtonElement).disabled === true
    if (!isDisabled()) {
        target.focus()
        return
    }
    const observer = new MutationObserver(() => {
        if (target.isConnected && isDisabled()) return
        stop()
        const focusLost = !document.activeElement || document.activeElement === document.body
        if (target.isConnected && focusLost) target.focus()
    })
    const timer = setTimeout(() => stop(), RESTORE_FOCUS_WAIT_MS)
    function stop() {
        observer.disconnect()
        clearTimeout(timer)
    }
    observer.observe(target, { attributes: true, attributeFilter: ['disabled'] })
}

function Modal({ isOpen, onClose, title, children, disabled = false, className = '' }: ModalProps) {
    const mouseDownOnOverlay = useRef(false)
    const contentRef = useRef<HTMLDivElement>(null)
    const titleId = useId()
    // Remember the trigger during render, before a child's autoFocus moves focus into the dialog.
    const [wasOpen, setWasOpen] = useState(isOpen)
    const [returnFocusTo, setReturnFocusTo] = useState<HTMLElement | null>(() =>
        isOpen ? (document.activeElement as HTMLElement | null) : null,
    )
    if (isOpen !== wasOpen) {
        setWasOpen(isOpen)
        if (isOpen) setReturnFocusTo(document.activeElement as HTMLElement | null)
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

    // Move focus into the dialog on open (unless a child autofocused) and give it back on close.
    useEffect(() => {
        if (!isOpen) return
        const content = contentRef.current
        if (content && !content.contains(document.activeElement)) {
            content.focus()
        }
        return () => {
            restoreFocus(returnFocusTo)
        }
    }, [isOpen, returnFocusTo])

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
