import './StatusBadge.scss'

interface StatusBadgeProps {
    status: string
    statusMap?: {
        [key: string]: {
            badgeClass: string
            text: string
        }
    }
}

function StatusBadge({ status, statusMap }: StatusBadgeProps) {
    const statusInfo = statusMap?.[status] || {
        badgeClass: 'badge-secondary',
        text: status,
    }

    return (
        <span className={`badge ${statusInfo.badgeClass}`}>
            {statusInfo.text}
        </span>
    )
}

export default StatusBadge

