import type { ReactNode } from 'react'
import { PageHeader as SharedPageHeader } from '@lostpointer/web-react'

interface PageHeaderProps {
    title: string
    action?: ReactNode
}

export default function PageHeader({ title, action }: PageHeaderProps) {
    return <SharedPageHeader title={title} actions={action} />
}
