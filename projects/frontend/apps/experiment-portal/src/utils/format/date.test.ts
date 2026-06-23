import { describe, it, expect } from 'vitest'
import { formatDate, formatDateTime, formatDuration, formatRelative } from './date'

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
