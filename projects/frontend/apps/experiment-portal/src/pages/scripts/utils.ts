import type { ExecutionStatus } from '../../types/scripts'

export function executionStatusClass(status: ExecutionStatus): string {
  switch (status) {
    case 'completed':
      return 'exec-status--completed'
    case 'failed':
    case 'timeout':
      return 'exec-status--failed'
    case 'running':
      return 'exec-status--running'
    case 'cancelled':
      return 'exec-status--cancelled'
    default:
      return 'exec-status--pending'
  }
}

export function executionStatusLabel(status: ExecutionStatus): string {
  const map: Record<ExecutionStatus, string> = {
    pending: 'Ожидание',
    running: 'Выполняется',
    completed: 'Завершён',
    failed: 'Ошибка',
    cancelled: 'Отменён',
    timeout: 'Таймаут',
  }
  return map[status] ?? status
}

export function calcDuration(started: string | null, finished: string | null): string {
  if (!started || !finished) return '—'
  const diff = (new Date(finished).getTime() - new Date(started).getTime()) / 1000
  return `${diff.toFixed(1)} с`
}
