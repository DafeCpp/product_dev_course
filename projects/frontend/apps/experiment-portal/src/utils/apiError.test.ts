import { describe, it, expect } from 'vitest'
import { HttpError } from '@lostpointer/web-http'
import { getApiErrorMessage, getResponseBodyMessage } from './apiError'

describe('getApiErrorMessage', () => {
    it('prefers response.data.error, then response.data.message', () => {
        expect(getApiErrorMessage({ response: { data: { error: 'A', message: 'B' } } }, 'fb')).toBe('A')
        expect(getApiErrorMessage({ response: { data: { message: 'B' } } }, 'fb')).toBe('B')
    })

    it('hides plain Error messages unless includeClientMessage is set', () => {
        expect(getApiErrorMessage(new Error('boom'), 'fb')).toBe('fb')
        expect(getApiErrorMessage(new Error('boom'), 'fb', { includeClientMessage: true })).toBe('boom')
    })


    it("skips axios' generic message", () => {
        const err = { isAxiosError: true, message: 'Request failed with status code 500', response: { data: {} } }
        expect(getApiErrorMessage(err, 'fb', { includeClientMessage: true })).toBe('fb')
    })

    it('ignores non-string payloads', () => {
        expect(getApiErrorMessage({ response: { data: { error: { code: 1 } } } }, 'fb')).toBe('fb')
        expect(getApiErrorMessage(null, 'fb')).toBe('fb')
        expect(getApiErrorMessage('oops', 'fb')).toBe('fb')
    })

    it('drops markup error pages', () => {
        for (const body of [
            '<!DOCTYPE html><html><body>502</body></html>',
            '<head><title>502 Bad Gateway</title></head><body>nginx</body>',
            '<?xml version="1.0"?><Error/>',
        ]) {
            expect(getApiErrorMessage({ response: { data: { error: body } } }, 'fb')).toBe('fb')
        }
    })

    it('collapses whitespace and control characters', () => {
        expect(getApiErrorMessage({ response: { data: { error: '  line1\n\tline2\u0007 ' } } }, 'fb')).toBe('line1 line2')
    })

    it('truncates very long messages', () => {
        const msg = getApiErrorMessage({ response: { data: { error: 'x'.repeat(1000) } } }, 'fb')
        expect(msg.length).toBe(300)
        expect(msg.endsWith('…')).toBe(true)
    })

    it('uses HttpError text only for api errors', () => {
        expect(getApiErrorMessage(new HttpError('api', 409, 'conflict', 'Проект уже существует'), 'fb')).toBe('Проект уже существует')
        expect(getApiErrorMessage(new HttpError('network', undefined, 'network', 'Network request failed'), 'fb')).toBe('fb')
        expect(getApiErrorMessage(new HttpError('timeout', undefined, 'timeout', 'Request timed out'), 'fb')).toBe('fb')
    })

    it('does not split surrogate pairs when truncating', () => {
        const msg = getApiErrorMessage({ response: { data: { error: 'x'.repeat(298) + '😀😀😀' } } }, 'fb')
        expect(msg).toBe('x'.repeat(298) + '😀…')
    })
})

describe('getResponseBodyMessage', () => {
    it('reads error/message strings from a JSON body', () => {
        expect(getResponseBodyMessage('{"error":"bad since_ts"}', 'fb')).toBe('bad since_ts')
        expect(getResponseBodyMessage('{"message":"nope"}', 'fb')).toBe('nope')
    })

    it('keeps the body text when JSON has no string error/message', () => {
        expect(getResponseBodyMessage('{"detail":"since_ts is invalid"}', 'fb')).toBe('{"detail":"since_ts is invalid"}')
    })

    it('uses plain text and drops markup', () => {
        expect(getResponseBodyMessage('sensor not found', 'fb')).toBe('sensor not found')
        expect(getResponseBodyMessage('<html>502</html>', 'fb')).toBe('fb')
        expect(getResponseBodyMessage('', 'fb')).toBe('fb')
    })
})
