import { describe, it, expect } from 'vitest'
import { getApiErrorMessage } from './apiError'

describe('getApiErrorMessage', () => {
    it('prefers response.data.error, then response.data.message', () => {
        expect(getApiErrorMessage({ response: { data: { error: 'A', message: 'B' } } }, 'fb')).toBe('A')
        expect(getApiErrorMessage({ response: { data: { message: 'B' } } }, 'fb')).toBe('B')
    })

    it('uses error.message for plain errors', () => {
        expect(getApiErrorMessage(new Error('boom'), 'fb')).toBe('boom')
    })

    it('skips error.message when serverOnly is set', () => {
        expect(getApiErrorMessage(new Error('boom'), 'fb', { serverOnly: true })).toBe('fb')
    })

    it("skips axios' generic message", () => {
        const err = { isAxiosError: true, message: 'Request failed with status code 500', response: { data: {} } }
        expect(getApiErrorMessage(err, 'fb')).toBe('fb')
    })

    it('ignores non-string payloads', () => {
        expect(getApiErrorMessage({ response: { data: { error: { code: 1 } } } }, 'fb')).toBe('fb')
        expect(getApiErrorMessage(null, 'fb')).toBe('fb')
        expect(getApiErrorMessage('oops', 'fb')).toBe('fb')
    })

    it('drops HTML error pages', () => {
        const err = { response: { data: { error: '<!DOCTYPE html><html><body>502</body></html>' } } }
        expect(getApiErrorMessage(err, 'fb')).toBe('fb')
    })

    it('collapses whitespace and control characters', () => {
        expect(getApiErrorMessage({ response: { data: { error: '  line1\n\tline2\u0007 ' } } }, 'fb')).toBe('line1 line2')
    })

    it('truncates very long messages', () => {
        const msg = getApiErrorMessage({ response: { data: { error: 'x'.repeat(1000) } } }, 'fb')
        expect(msg.length).toBe(300)
        expect(msg.endsWith('…')).toBe(true)
    })
})
