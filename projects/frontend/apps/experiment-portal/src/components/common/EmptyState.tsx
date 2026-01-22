import { ReactNode } from 'react'
import './EmptyState.scss'

interface EmptyStateProps {
    message: string
    children?: ReactNode
}

function EmptyState({ message, children }: EmptyStateProps) {
    return (
        <div className="empty-state">
            <p>{message}</p>
            {children}
        </div>
    )
}

export default EmptyState

