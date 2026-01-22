import type { MouseEventHandler } from 'react'
import './FloatingActionButton.scss'

type FloatingActionButtonProps = {
    onClick: MouseEventHandler<HTMLButtonElement>
    title: string
    ariaLabel: string
    disabled?: boolean
}

function FloatingActionButton({
    onClick,
    title,
    ariaLabel,
    disabled = false,
}: FloatingActionButtonProps) {
    return (
        <button
            className="fab"
            onClick={onClick}
            title={title}
            aria-label={ariaLabel}
            disabled={disabled}
        >
            <svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">
                <path
                    d="M12 5c.552 0 1 .448 1 1v5h5c.552 0 1 .448 1 1s-.448 1-1 1h-5v5c0 .552-.448 1-1 1s-1-.448-1-1v-5H6c-.552 0-1-.448-1-1s.448-1 1-1h5V6c0-.552.448-1 1-1z"
                    fill="currentColor"
                />
            </svg>
        </button>
    )
}

export default FloatingActionButton
