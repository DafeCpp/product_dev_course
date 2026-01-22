import { ReactNode } from 'react'
import './PageHeader.scss'

interface PageHeaderProps {
    title: string
    action?: ReactNode
}

function PageHeader({ title, action }: PageHeaderProps) {
    return (
        <div className="page-header">
            <h2>{title}</h2>
            {action && <div className="page-header-action">{action}</div>}
        </div>
    )
}

export default PageHeader

