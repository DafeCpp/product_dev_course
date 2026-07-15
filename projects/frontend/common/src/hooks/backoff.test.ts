import { describe, it, expect, vi, afterEach } from 'vitest'
import { computeBackoffDelayMs } from './backoff'

describe('computeBackoffDelayMs', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('grows exponentially with attempt count when jitter is zeroed out', () => {
    vi.spyOn(Math, 'random').mockReturnValue(0.5) // (0.5*2-1) = 0 -> no jitter
    const opts = { initialDelayMs: 1000, factor: 2, maxDelayMs: 100000, jitterRatio: 0.2 }
    expect(computeBackoffDelayMs(1, opts)).toBe(1000)
    expect(computeBackoffDelayMs(2, opts)).toBe(2000)
    expect(computeBackoffDelayMs(3, opts)).toBe(4000)
    expect(computeBackoffDelayMs(4, opts)).toBe(8000)
  })

  it('caps the delay at maxDelayMs once the exponential term exceeds it', () => {
    vi.spyOn(Math, 'random').mockReturnValue(0.5)
    const opts = { initialDelayMs: 1000, factor: 2, maxDelayMs: 5000, jitterRatio: 0.2 }
    expect(computeBackoffDelayMs(10, opts)).toBe(5000)
  })

  it('applies positive jitter up to the configured ratio', () => {
    vi.spyOn(Math, 'random').mockReturnValue(1) // (1*2-1) = 1 -> +jitterRatio
    const opts = { initialDelayMs: 1000, factor: 2, maxDelayMs: 100000, jitterRatio: 0.2 }
    expect(computeBackoffDelayMs(1, opts)).toBe(1200)
  })

  it('applies negative jitter down to the configured ratio, never going negative', () => {
    vi.spyOn(Math, 'random').mockReturnValue(0) // (0*2-1) = -1 -> -jitterRatio
    const opts = { initialDelayMs: 1000, factor: 2, maxDelayMs: 100000, jitterRatio: 0.2 }
    expect(computeBackoffDelayMs(1, opts)).toBe(800)
  })

  it('never returns a negative delay even with maximal negative jitter and a tiny base', () => {
    vi.spyOn(Math, 'random').mockReturnValue(0)
    const opts = { initialDelayMs: 10, factor: 1, maxDelayMs: 10, jitterRatio: 2 }
    expect(computeBackoffDelayMs(1, opts)).toBeGreaterThanOrEqual(0)
  })

  it('uses sensible defaults when no options are given', () => {
    vi.spyOn(Math, 'random').mockReturnValue(0.5)
    expect(computeBackoffDelayMs(1)).toBe(1000)
    expect(computeBackoffDelayMs(2)).toBe(2000)
  })
})
