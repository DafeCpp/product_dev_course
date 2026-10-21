import { describe, it, expect } from 'vitest'
import {
  formatCountdown,
  formatSecondsDuration,
  formatSessionDuration,
  formatDate,
  formatDateTime,
  formatDateTimeSeconds,
  formatDuration,
  formatRelative,
  formatShortDateTime,
} from './date'

describe('formatDate', () => {
  it('formats Date object as dd.MM.yyyy', () => {
    const date = new Date('2026-04-27T12:30:45Z')
    const result = formatDate(date)
    expect(result).toBe('27.04.2026')
  })

  it('formats ISO string as dd.MM.yyyy', () => {
    const result = formatDate('2026-04-27T12:30:45Z')
    expect(result).toBe('27.04.2026')
  })

  it('formats timestamp (ms) as dd.MM.yyyy', () => {
    const timestamp = new Date('2026-04-27T12:30:45Z').getTime()
    const result = formatDate(timestamp)
    expect(result).toBe('27.04.2026')
  })

  it('returns "—" for invalid dates', () => {
    expect(formatDate(new Date('invalid'))).toBe('—')
    expect(formatDate('invalid')).toBe('—')
  })
})

describe('formatDateTime', () => {
  it('formats Date object as dd.MM.yyyy HH:mm', () => {
    const date = new Date('2026-04-27T15:30:45Z')
    const result = formatDateTime(date)
    // Note: actual time may vary by timezone, so check format
    expect(result).toMatch(/^\d{2}\.\d{2}\.\d{4} \d{2}:\d{2}$/)
  })

  it('formats ISO string as dd.MM.yyyy HH:mm', () => {
    const result = formatDateTime('2026-04-27T15:30:45Z')
    expect(result).toMatch(/^\d{2}\.\d{2}\.\d{4} \d{2}:\d{2}$/)
  })

  it('returns "—" for invalid dates', () => {
    expect(formatDateTime(new Date('invalid'))).toBe('—')
  })
})

describe('formatDuration', () => {
  it('formats milliseconds as human-readable duration', () => {
    expect(formatDuration(0)).toBe('0с')
    expect(formatDuration(1000)).toBe('1с')
    expect(formatDuration(60 * 1000)).toBe('1м')
    expect(formatDuration(90 * 1000)).toBe('1м 30с')
    expect(formatDuration(3600 * 1000)).toBe('1ч')
    expect(formatDuration(3600 * 1000 + 23 * 60 * 1000 + 45 * 1000)).toBe('1ч 23м 45с')
  })

  it('rounds to nearest second', () => {
    expect(formatDuration(1500)).toBe('2с')
    expect(formatDuration(400)).toBe('0с')
  })

  it('returns "—" for negative or invalid values', () => {
    expect(formatDuration(-1000)).toBe('—')
    expect(formatDuration(Infinity)).toBe('—')
    expect(formatDuration(NaN)).toBe('—')
  })
})

describe('formatRelative', () => {
  it('formats recent date with addSuffix', () => {
    const now = new Date()
    const oneMinuteAgo = new Date(now.getTime() - 60 * 1000)
    const result = formatRelative(oneMinuteAgo)
    expect(result).toContain('назад')
  })

  it('accepts ISO string', () => {
    const oneDayAgo = new Date(new Date().getTime() - 24 * 60 * 60 * 1000).toISOString()
    const result = formatRelative(oneDayAgo)
    expect(result).toMatch(/назад/)
  })

  it('returns "—" for invalid dates', () => {
    expect(formatRelative(new Date('invalid'))).toBe('—')
  })
})

describe('formatDateTimeSeconds', () => {
  // Local-time Date, so the expectation holds in any timezone.
  const local = new Date(2026, 3, 27, 15, 30, 45)

  it('formats as dd.MM.yyyy HH:mm:ss', () => {
    expect(formatDateTimeSeconds(local)).toBe('27.04.2026 15:30:45')
  })

  it('returns "—" for invalid dates', () => {
    expect(formatDateTimeSeconds('invalid')).toBe('—')
  })
})

describe('formatShortDateTime', () => {
  const local = new Date(2026, 3, 27, 15, 30, 45)

  it('formats as dd.MM HH:mm without the year', () => {
    expect(formatShortDateTime(local)).toBe('27.04 15:30')
  })

  it('adds seconds on request', () => {
    expect(formatShortDateTime(local, { seconds: true })).toBe('27.04 15:30:45')
  })

  it('returns "—" for invalid dates', () => {
    expect(formatShortDateTime(new Date('invalid'))).toBe('—')
  })
})

describe('formatSecondsDuration', () => {
  it('renders missing or zero as "-"', () => {
    expect(formatSecondsDuration(null)).toBe('-')
    expect(formatSecondsDuration(undefined)).toBe('-')
    expect(formatSecondsDuration(0)).toBe('-')
  })

  it('keeps every unit below the largest one', () => {
    expect(formatSecondsDuration(5)).toBe('5с')
    expect(formatSecondsDuration(125)).toBe('2м 5с')
    expect(formatSecondsDuration(3661)).toBe('1ч 1м 1с')
    expect(formatSecondsDuration(3605)).toBe('1ч 0м 5с')
  })
})

describe('formatSessionDuration', () => {
  it('measures closed sessions', () => {
    expect(formatSessionDuration(null, null)).toBe('-')
    expect(formatSessionDuration('2024-01-01T10:00:00Z', '2024-01-01T10:00:00Z')).toBe('-')
    expect(formatSessionDuration('2024-01-01T10:00:00Z', '2024-01-01T10:02:05Z')).toBe('2м 5с')
    expect(formatSessionDuration('2024-01-01T10:00:00Z', '2024-01-01T11:01:01Z')).toBe('1ч 1м 1с')
  })

  it('measures an open session up to now', () => {
    const started = new Date(Date.now() - 90_000).toISOString()
    expect(formatSessionDuration(started)).toMatch(/^1м 3\dс$/)
  })
})

describe('formatCountdown', () => {
  it('pads, rounds up and clamps at zero', () => {
    expect(formatCountdown(61_000)).toBe('01:01')
    expect(formatCountdown(500)).toBe('00:01')
    expect(formatCountdown(-5)).toBe('00:00')
  })
})
