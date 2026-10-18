import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { act, renderHook } from '@testing-library/react'

const notifyError = vi.fn()
vi.mock('../utils/notify', () => ({ notifyError: (m: string) => notifyError(m) }))

import { useCountdown } from './useCountdown'

const NOW = new Date('2026-01-01T12:00:00Z').getTime()

describe('useCountdown', () => {
  beforeEach(() => { vi.clearAllMocks(); vi.useFakeTimers(); vi.setSystemTime(NOW) })
  afterEach(() => vi.useRealTimers())

  it('is disabled without a deadline', () => {
    const { result } = renderHook(() => useCountdown(null))
    expect(result.current).toEqual({ remaining: null, isWarning: false, isExpired: false })
  })

  it('counts down without warning when plenty of time is left', () => {
    const { result } = renderHook(() => useCountdown(NOW + 10 * 60 * 1000))
    expect(result.current.remaining).toBe(600_000)
    act(() => { vi.advanceTimersByTime(2000) })
    expect(result.current.remaining).toBe(598_000)
    expect(result.current.isWarning).toBe(false)
    expect(notifyError).not.toHaveBeenCalled()
  })

  it('warns once when under five minutes remain, then expires', () => {
    const { result } = renderHook(() => useCountdown(NOW + 5 * 60 * 1000 + 2000))
    expect(result.current.isWarning).toBe(false)
    act(() => { vi.advanceTimersByTime(3000) })
    expect(result.current.isWarning).toBe(true)
    act(() => { vi.advanceTimersByTime(60_000) })
    expect(notifyError).toHaveBeenCalledTimes(1)
    act(() => { vi.advanceTimersByTime(5 * 60 * 1000) })
    expect(result.current.isExpired).toBe(true)
    expect(result.current.isWarning).toBe(false)
  })

  it('resets when the deadline is cleared and stops ticking after unmount', () => {
    const { result, rerender, unmount } = renderHook(({ d }) => useCountdown(d), { initialProps: { d: NOW + 60_000 as number | null } })
    expect(notifyError).toHaveBeenCalledTimes(1)
    rerender({ d: null })
    expect(result.current.remaining).toBeNull()
    rerender({ d: NOW + 30_000 })
    expect(notifyError).toHaveBeenCalledTimes(2)   // warning armed again after the reset
    unmount()
    const timers = vi.getTimerCount()
    expect(timers).toBe(0)
  })
})
