import { describe, it, expect, beforeEach } from 'vitest'
import { getCookie, getCsrfToken } from './csrf'
import { getTraceId, resetTraceId } from './trace'
import { streamBodyError, streamResponseError, getStreamErrorMessage } from './streamError'
import { UserFacingError } from './apiError'

function setCookie(value: string) {
  Object.defineProperty(document, 'cookie', { value, configurable: true, writable: true })
}

describe('csrf cookies', () => {
  beforeEach(() => setCookie(''))
  it('returns undefined for empty or missing cookies', () => {
    expect(getCookie('a')).toBeUndefined()
    setCookie('a=1')
    expect(getCookie('b')).toBeUndefined()
  })
  it('parses and decodes cookie values, skipping malformed parts', () => {
    setCookie('; broken; a%20b=x%2Fy ; csrf_token=tok%3D')
    expect(getCookie('a b')).toBe('x/y')
    expect(getCsrfToken()).toBe('tok=')
  })
})

describe('trace id', () => {
  it('is stable until reset', () => {
    const first = getTraceId()
    expect(getTraceId()).toBe(first)
    resetTraceId()
    expect(getTraceId()).not.toBe(first)
  })
})

describe('stream errors', () => {
  it('builds a user-facing error from a body', () => {
    expect(streamBodyError('', 500)).toBeInstanceOf(UserFacingError)
  })
  it('reads the response body, tolerating a failing text()', async () => {
    const ok = await streamResponseError({ text: async () => '{"detail":"nope"}', status: 400 } as Response)
    expect(ok).toBeInstanceOf(UserFacingError)
    const bad = await streamResponseError({ text: () => Promise.reject(new Error('x')), status: 502 } as Response)
    expect(bad).toBeInstanceOf(UserFacingError)
  })
  it('falls back to a generic message for unknown errors', () => {
    expect(getStreamErrorMessage(new TypeError('failed to fetch'))).toBe('Ошибка подключения к стриму')
    expect(getStreamErrorMessage(new UserFacingError('Нельзя'))).toBe('Нельзя')
  })
})
