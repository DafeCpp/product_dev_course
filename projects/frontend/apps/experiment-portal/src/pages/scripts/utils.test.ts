import { describe, it, expect } from 'vitest'
import { calcDuration, executionStatusClass, executionStatusLabel } from './utils'
import type { ExecutionStatus } from '../../types/scripts'

describe('scripts utils', () => {
  it.each([
    ['completed', 'exec-status--completed'],
    ['failed', 'exec-status--failed'],
    ['timeout', 'exec-status--failed'],
    ['running', 'exec-status--running'],
    ['cancelled', 'exec-status--cancelled'],
    ['pending', 'exec-status--pending'],
  ] as [ExecutionStatus, string][])('status %s -> class %s', (status, cls) => {
    expect(executionStatusClass(status)).toBe(cls)
  })

  it('falls back to the pending class for an unknown status', () => {
    expect(executionStatusClass('weird' as ExecutionStatus)).toBe('exec-status--pending')
  })

  it('translates statuses and passes unknown ones through', () => {
    expect(executionStatusLabel('completed')).toBe('Завершён')
    expect(executionStatusLabel('timeout')).toBe('Таймаут')
    expect(executionStatusLabel('weird' as ExecutionStatus)).toBe('weird')
  })

  it('calculates duration or a dash when a bound is missing', () => {
    expect(calcDuration('2026-01-01T00:00:00Z', '2026-01-01T00:00:02.5Z')).toBe('2.5 с')
    expect(calcDuration(null, '2026-01-01T00:00:02Z')).toBe('—')
    expect(calcDuration('2026-01-01T00:00:00Z', null)).toBe('—')
  })
})
