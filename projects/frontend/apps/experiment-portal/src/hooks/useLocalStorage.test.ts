import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { renderHook, act } from '@testing-library/react'
import { z } from 'zod'
import { useLocalStorage } from './useLocalStorage'

beforeEach(() => localStorage.clear())
afterEach(() => localStorage.clear())

describe('useLocalStorage — basic read/write', () => {
  it('returns defaultValue when key is absent', () => {
    const { result } = renderHook(() => useLocalStorage('k', 42))
    expect(result.current[0]).toBe(42)
  })

  it('persists value to localStorage', () => {
    const { result } = renderHook(() => useLocalStorage('k', 0))
    act(() => result.current[1](99))
    expect(result.current[0]).toBe(99)
    expect(JSON.parse(localStorage.getItem('k')!)).toBe(99)
  })

  it('accepts functional updater', () => {
    const { result } = renderHook(() => useLocalStorage('k', 10))
    act(() => result.current[1]((prev) => prev + 5))
    expect(result.current[0]).toBe(15)
  })

  it('removes key and resets to default', () => {
    const { result } = renderHook(() => useLocalStorage('k', 'default'))
    act(() => result.current[1]('custom'))
    act(() => result.current[2]())
    expect(result.current[0]).toBe('default')
    expect(localStorage.getItem('k')).toBeNull()
  })

  it('loads pre-existing value from storage on mount', () => {
    localStorage.setItem('k', JSON.stringify({ a: 1 }))
    const { result } = renderHook(() => useLocalStorage<{ a: number }>('k', { a: 0 }))
    expect(result.current[0]).toEqual({ a: 1 })
  })
})

describe('useLocalStorage — broken JSON fallback', () => {
  it('returns defaultValue when stored JSON is malformed', () => {
    localStorage.setItem('k', 'not-json{{')
    const { result } = renderHook(() => useLocalStorage('k', 'fallback'))
    expect(result.current[0]).toBe('fallback')
  })
})

describe('useLocalStorage — Zod schema validation', () => {
  const schema = z.object({ count: z.number() })

  it('accepts valid stored data', () => {
    localStorage.setItem('k', JSON.stringify({ count: 7 }))
    const { result } = renderHook(() => useLocalStorage('k', { count: 0 }, schema))
    expect(result.current[0]).toEqual({ count: 7 })
  })

  it('falls back to default when stored data fails schema', () => {
    localStorage.setItem('k', JSON.stringify({ count: 'not-a-number' }))
    const { result } = renderHook(() => useLocalStorage('k', { count: 0 }, schema))
    expect(result.current[0]).toEqual({ count: 0 })
  })

  it('falls back to default when stored data is wrong type entirely', () => {
    localStorage.setItem('k', JSON.stringify('string-not-object'))
    const { result } = renderHook(() => useLocalStorage('k', { count: 0 }, schema))
    expect(result.current[0]).toEqual({ count: 0 })
  })
})
