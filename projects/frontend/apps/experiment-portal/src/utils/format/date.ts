import { format, formatDistanceToNow, parseISO, isValid } from 'date-fns'
import { ru } from 'date-fns/locale'

type DateInput = Date | string | number

function ensureDate(input: DateInput): Date {
  if (input instanceof Date) {
    return input
  }
  if (typeof input === 'string') {
    return parseISO(input)
  }
  if (typeof input === 'number') {
    return new Date(input)
  }
  return new Date()
}

/**
 * Format date as dd.MM.yyyy
 */
export function formatDate(input: DateInput): string {
  const date = ensureDate(input)
  if (!isValid(date)) {
    return '—'
  }
  return format(date, 'dd.MM.yyyy', { locale: ru })
}

/**
 * Format date and time as dd.MM.yyyy HH:mm
 */
export function formatDateTime(input: DateInput): string {
  const date = ensureDate(input)
  if (!isValid(date)) {
    return '—'
  }
  return format(date, 'dd.MM.yyyy HH:mm', { locale: ru })
}

/**
 * Format milliseconds as human-readable duration (e.g. "1ч 23м 45с")
 */
export function formatDuration(ms: number): string {
  if (ms < 0 || !Number.isFinite(ms)) {
    return '—'
  }

  const totalSeconds = Math.round(ms / 1000)
  const hours = Math.floor(totalSeconds / 3600)
  const minutes = Math.floor((totalSeconds % 3600) / 60)
  const seconds = totalSeconds % 60

  const parts: string[] = []
  if (hours > 0) {
    parts.push(`${hours}ч`)
  }
  if (minutes > 0) {
    parts.push(`${minutes}м`)
  }
  if (seconds > 0 || parts.length === 0) {
    parts.push(`${seconds}с`)
  }

  return parts.join(' ')
}

/**
 * Format date relative to now (e.g. "2 минуты назад")
 */
export function formatRelative(input: DateInput): string {
  const date = ensureDate(input)
  if (!isValid(date)) {
    return '—'
  }
  return formatDistanceToNow(date, { locale: ru, addSuffix: true })
}
