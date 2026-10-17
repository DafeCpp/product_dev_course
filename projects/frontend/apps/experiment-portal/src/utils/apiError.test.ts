import { describe, it, expect } from 'vitest'
import { HttpError } from '@lostpointer/web-http'
import { UserFacingError, getApiErrorMessage, getResponseBodyMessage, isNotFoundError } from './apiError'

describe('getApiErrorMessage', () => {
    it('prefers response.data.message, then response.data.error', () => {
        const body = { error: 'password_change_required', message: 'Смените пароль' }
        expect(getApiErrorMessage({ response: { data: body } }, 'fb')).toBe('Смените пароль')
        expect(getApiErrorMessage({ response: { data: { error: 'A' } } }, 'fb')).toBe('A')
    })

    it('keeps messages that merely start with a placeholder', () => {
        for (const msg of ['<sensor_id> must be a UUID', '<body-temp> must be a number', '<p.value> required', '<path> must not contain </', '<name> must not contain </script>', '<p> must be between 0 and 1', '<a> is required']) {
            expect(getApiErrorMessage({ response: { data: { error: msg } } }, 'fb')).toBe(msg)
        }
    })

    it('never shows 5xx body text', () => {
        const proxy = { isAxiosError: true, response: { status: 502, data: { statusCode: 502, error: 'Bad Gateway', message: 'connect ECONNREFUSED 10.0.0.5:8001' } } }
        expect(getApiErrorMessage(proxy, 'fb')).toBe('fb')
        expect(getApiErrorMessage(new HttpError('api', 503, 'unavailable', 'upstream timeout at 10.0.0.5'), 'fb')).toBe('fb')
        expect(getApiErrorMessage({ response: { status: 409, data: { error: 'Уже существует' } } }, 'fb')).toBe('Уже существует')
    })

    it('uses a plain-text response body', () => {
        expect(getApiErrorMessage({ isAxiosError: true, response: { data: 'sensor not found' } }, 'fb')).toBe('sensor not found')
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
            '<!-- cdn --><p>Service unavailable</p>',
            '<meta charset="utf-8"><title>503</title></head>',
            '<html><body>Bad Gateway<br>',
            '<Error code="x"/>',
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

    it('falls back on the synthetic HTTP <status> message of an HttpError', () => {
        expect(getApiErrorMessage(new HttpError('api', 500, 'http_error', 'HTTP 500'), 'fb')).toBe('fb')
    })

    it('always shows a UserFacingError message', () => {
        expect(getApiErrorMessage(new UserFacingError('Ошибка стрима: HTTP 404'), 'fb')).toBe('Ошибка стрима: HTTP 404')
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

    it('falls back instead of showing raw JSON without a string error/message', () => {
        expect(getResponseBodyMessage('{"detail":"since_ts is invalid"}', 'fb')).toBe('fb')
        expect(getResponseBodyMessage('{"error":{"code":"x"}}', 'fb')).toBe('fb')
    })

    it('handles JSON scalar bodies', () => {
        expect(getResponseBodyMessage('"sensor not found"', 'fb')).toBe('sensor not found')
        expect(getResponseBodyMessage('null', 'fb')).toBe('fb')
        expect(getResponseBodyMessage('42', 'fb')).toBe('fb')
    })

    it('uses plain text and drops markup', () => {
        expect(getResponseBodyMessage('sensor not found', 'fb')).toBe('sensor not found')
        expect(getResponseBodyMessage('<html>502</html>', 'fb')).toBe('fb')
        expect(getResponseBodyMessage('', 'fb')).toBe('fb')
    })
})

describe('getApiErrorMessage with text response bodies', () => {
    it('parses a JSON body delivered as a string', () => {
        const err = { isAxiosError: true, response: { status: 400, data: '{"error":"bad","message":"Неверный формат"}' } }
        expect(getApiErrorMessage(err, 'fb')).toBe('Неверный формат')
    })

    it('falls back for JSON text without a string message', () => {
        const err = { isAxiosError: true, response: { status: 400, data: '{"detail":"x"}' } }
        expect(getApiErrorMessage(err, 'fb')).toBe('fb')
    })
})

describe('isNotFoundError', () => {
    it('detects 404 from HttpError and axios errors', () => {
        expect(isNotFoundError(new HttpError('api', 404, 'not_found', 'HTTP 404'))).toBe(true)
        expect(isNotFoundError({ response: { status: 404 } })).toBe(true)
        expect(isNotFoundError({ response: { status: 403 } })).toBe(false)
        expect(isNotFoundError(new Error('x'))).toBe(false)
        expect(isNotFoundError(null)).toBe(false)
    })
})
