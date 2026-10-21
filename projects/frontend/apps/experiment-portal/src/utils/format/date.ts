import { format, formatDistanceToNow, parseISO, isValid } from 'date-fns'
import { ru } from 'date-fns/locale'

type DateInput = Date | string | number

function ensureDate(input: DateInput): Date {
  if (input instanceof Date) return input
  if (typeof input === 'string') return parseISO(input)
  return new Date(input)
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
 * Format date and time with seconds as dd.MM.yyyy HH:mm:ss
 */
export function formatDateTimeSeconds(input: DateInput): string {
  const date = ensureDate(input)
  if (!isValid(date)) {
    return '—'
  }
  return format(date, 'dd.MM.yyyy HH:mm:ss', { locale: ru })
}

/**
 * Format day, month and time without the year as dd.MM HH:mm (or dd.MM HH:mm:ss with seconds)
 */
export function formatShortDateTime(input: DateInput, { seconds = false } = {}): string {
  const date = ensureDate(input)
  if (!isValid(date)) {
    return '—'
  }
  return format(date, seconds ? 'dd.MM HH:mm:ss' : 'dd.MM HH:mm', { locale: ru })
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

/**
 * Format whole seconds as "1ч 2м 3с" / "2м 3с" / "3с" (all units below the
 * largest are kept). Missing or zero seconds render as "-".
 */
export function formatSecondsDuration(seconds?: number | null): string {
  if (!seconds) return '-'
  const hours = Math.floor(seconds / 3600)
  const minutes = Math.floor((seconds % 3600) / 60)
  const secs = seconds % 60
  if (hours > 0) return `${hours}ч ${minutes}м ${secs}с`
  if (minutes > 0) return `${minutes}м ${secs}с`
  return `${secs}с`
}

/** Duration of an interval; a missing end means it is still running (until now). */
export function formatSessionDuration(startedAt?: string | null, stoppedAt?: string | null): string {
  if (!startedAt) return '-'
  const end = stoppedAt ? new Date(stoppedAt) : new Date()
  return formatSecondsDuration(Math.floor((end.getTime() - new Date(startedAt).getTime()) / 1000))
}

/** Format remaining milliseconds as a "MM:SS" countdown, rounding up and clamping at zero. */
export function formatCountdown(ms: number): string {
  const totalSeconds = Math.max(0, Math.ceil(ms / 1000))
  const minutes = Math.floor(totalSeconds / 60)
  const seconds = totalSeconds % 60
  return `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`
}
