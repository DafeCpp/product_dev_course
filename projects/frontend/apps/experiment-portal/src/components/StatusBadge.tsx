import './StatusBadge.css'

interface StatusBadgeProps {
  status: string
  variant?: 'experiment' | 'run'
}

function StatusBadge({ status, variant = 'experiment' }: StatusBadgeProps) {
  const getStatusBadge = (status: string) => {
    const badges: Record<string, string> = {
      created: 'badge-secondary',
      running: 'badge-info',
      completed: 'badge-success',
      failed: 'badge-danger',
      archived: 'badge-secondary',
    }
    return badges[status] || 'badge-secondary'
  }

  const getStatusText = (status: string) => {
    if (variant === 'run') {
      const texts: Record<string, string> = {
        created: 'Создан',
        running: 'Выполняется',
        completed: 'Завершен',
        failed: 'Ошибка',
      }
      return texts[status] || status
    }

    const texts: Record<string, string> = {
      created: 'Создан',
      running: 'Выполняется',
      completed: 'Завершен',
      failed: 'Ошибка',
      archived: 'Архивирован',
    }
    return texts[status] || status
  }

  return (
    <span className={`badge ${getStatusBadge(status)}`}>
      {getStatusText(status)}
    </span>
  )
}

export default StatusBadge
