import './StatusBadge.css'

interface StatusBadgeProps {
  status: string
  variant?: 'experiment' | 'run' | 'sensor' | 'capture-session'
}

function StatusBadge({ status, variant = 'experiment' }: StatusBadgeProps) {
  const getStatusBadge = (status: string) => {
    if (variant === 'sensor') {
      const badges: Record<string, string> = {
        registering: 'badge-secondary',
        active: 'badge-success',
        inactive: 'badge-warning',
        archived: 'badge-secondary',
      }
      return badges[status] || 'badge-secondary'
    }

    if (variant === 'capture-session') {
      const badges: Record<string, string> = {
        draft: 'badge-secondary',
        running: 'badge-info',
        succeeded: 'badge-success',
        failed: 'badge-danger',
        archived: 'badge-secondary',
        backfilling: 'badge-warning',
      }
      return badges[status] || 'badge-secondary'
    }

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

    if (variant === 'sensor') {
      const texts: Record<string, string> = {
        registering: 'Регистрация',
        active: 'Активен',
        inactive: 'Неактивен',
        archived: 'Архивирован',
      }
      return texts[status] || status
    }

    if (variant === 'capture-session') {
      const texts: Record<string, string> = {
        draft: 'Черновик',
        running: 'Выполняется',
        succeeded: 'Успешно',
        failed: 'Ошибка',
        archived: 'Архивирован',
        backfilling: 'Дозаполнение',
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

