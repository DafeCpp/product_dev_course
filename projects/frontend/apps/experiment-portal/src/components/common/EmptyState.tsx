import type { ReactNode } from 'react'
import { EmptyState as SharedEmptyState } from '@lostpointer/web-react'

interface EmptyStateProps {
    message: string
    children?: ReactNode
}

export default function EmptyState({ message, children }: EmptyStateProps) {
    return <SharedEmptyState title={message}>{children}</SharedEmptyState>
}
