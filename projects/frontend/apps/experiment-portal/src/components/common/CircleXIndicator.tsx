import './CircleXIndicator.css'

type CircleXIndicatorProps = {
    title?: string
    ariaLabel?: string
    variant?: 'fab' | 'inline'
    className?: string
}

function CircleXIndicator({
    title,
    ariaLabel,
    variant = 'inline',
    className = '',
}: CircleXIndicatorProps) {
    const label = ariaLabel ?? title ?? 'Недоступно'

    return (
        <div
            className={`circle-x circle-x--${variant} ${className}`.trim()}
            role="img"
            aria-label={label}
            title={title ?? label}
        >
            <svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">
                <path
                    d="M7.05 7.05a1 1 0 0 1 1.414 0L12 10.586l3.536-3.536a1 1 0 1 1 1.414 1.414L13.414 12l3.536 3.536a1 1 0 1 1-1.414 1.414L12 13.414l-3.536 3.536a1 1 0 0 1-1.414-1.414L10.586 12 7.05 8.464a1 1 0 0 1 0-1.414z"
                    fill="currentColor"
                />
            </svg>
        </div>
    )
}

export default CircleXIndicator
