import type { ConnectionStatus } from '../../types'
import './ConnectionStatusIndicator.scss'

interface ConnectionStatusIndicatorProps {
  status: ConnectionStatus | undefined
  showLabel?: boolean
}

const statusConfig: Record<ConnectionStatus, { color: string; label: string; cssClass: string }> = {
  online: { color: '#16a34a', label: 'Онлайн', cssClass: 'conn-indicator--online' },
  delayed: { color: '#d97706', label: 'Задержка', cssClass: 'conn-indicator--delayed' },
  offline: { color: '#6b7280', label: 'Оффлайн', cssClass: 'conn-indicator--offline' },
}

function ConnectionStatusIndicator({ status, showLabel = false }: ConnectionStatusIndicatorProps) {
  if (!status) return null
  const config = statusConfig[status]
  return (
    <span className={`conn-indicator ${config.cssClass}`} title={config.label}>
      <span className="conn-indicator__dot" aria-hidden="true" />
      {showLabel && <span className="conn-indicator__label">{config.label}</span>}
    </span>
  )
}

export default ConnectionStatusIndicator
