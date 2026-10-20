import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import {
    sanitizeHeaders,
    sanitizeBody,
    formatHttpDebugText,
    buildHttpDebugInfoFromAxiosError,
    buildHttpDebugInfoFromFetch,
    maybeEmitHttpErrorToast,
    type HttpDebugInfo,
} from './httpDebug'

const emitToast = vi.fn()
vi.mock('./toastBus', () => ({ emitToast: (e: unknown) => emitToast(e) }))

describe('sanitizeHeaders shapes', () => {
    it('returns empty object for non-object input', () => {
        expect(sanitizeHeaders(undefined)).toEqual({})
        expect(sanitizeHeaders('x')).toEqual({})
    })

    it('projects numbers, booleans, arrays and objects; drops null/undefined', () => {
        const out = sanitizeHeaders({
            a: 1,
            b: true,
            c: ['x', 2],
            d: { toString: () => 'obj' },
            e: null,
            f: undefined,
            'X-Client-Secret': 's',
            long: 'z'.repeat(9000),
        })
        expect(out.a).toBe('1')
        expect(out.b).toBe('true')
        expect(out.c).toBe('x, 2')
        expect(out.d).toBe('obj')
        expect(out).not.toHaveProperty('e')
        expect(out).not.toHaveProperty('f')
        expect(out['X-Client-Secret']).toBe('[REDACTED]')
        expect(out.long).toContain('[truncated 808 chars]')
    })
})

describe('sanitizeBody edge cases', () => {
    it('passes through primitives and null/undefined', () => {
        expect(sanitizeBody(null)).toBeNull()
        expect(sanitizeBody(undefined)).toBeUndefined()
        expect(sanitizeBody(3)).toBe(3)
        expect(sanitizeBody(false)).toBe(false)
        expect(sanitizeBody('s')).toBe('s')
    })

    it('masks keys containing secret/password/token and caps arrays at 200', () => {
        const out = sanitizeBody({
            clientSecret: 'a',
            userPassword: 'b',
            myToken: 'c',
            list: Array.from({ length: 250 }, (_, i) => i),
        }) as Record<string, unknown>
        expect(out.clientSecret).toBe('[REDACTED]')
        expect(out.userPassword).toBe('[REDACTED]')
        expect(out.myToken).toBe('[REDACTED]')
        expect((out.list as number[]).length).toBe(200)
    })

    it('stops at max depth', () => {
        const deep = { a: { b: { c: { d: { e: { f: { g: { h: 1 } } } } } } } }
        const out = JSON.stringify(sanitizeBody(deep))
        expect(out).toContain('[MaxDepth]')
    })

    it('stringifies non-plain values and handles unserializable ones', () => {
        expect(sanitizeBody(Symbol('x'))).toBe('Symbol(x)')
        const fn = () => undefined
        fn.toString = () => {
            throw new Error('nope')
        }
        expect(sanitizeBody(fn)).toBe('[Unserializable]')
    })
})

describe('formatHttpDebugText', () => {
    it('renders a minimal request without response/correlation', () => {
        const text = formatHttpDebugText({ kind: 'http', message: 'm', request: {} })
        expect(text).toBe('Message: m\n\nRequest:\n  method: \n  url:')
    })

    it('renders all optional request/response/correlation fields', () => {
        const text = formatHttpDebugText({
            kind: 'http',
            message: 'm',
            request: {
                method: 'POST',
                url: '/x',
                baseURL: 'http://b',
                params: { q: 1 },
                headers: {},
                body: 'raw body',
            },
            response: { statusText: 'Bad', headers: { a: 'b' }, body: { e: 1 } },
            correlation: { traceparent: 'tp', x_trace_id: 'xt' },
        })
        expect(text).toContain('baseURL: http://b')
        expect(text).toContain('params: {')
        expect(text).not.toContain('  headers: {}')
        expect(text).toContain('body: raw body')
        expect(text).not.toContain('status: ')
        expect(text).toContain('statusText: Bad')
        expect(text).toContain('traceparent: tp')
        expect(text).toContain('x-trace-id: xt')
    })

    it('falls back to String() when JSON.stringify throws', () => {
        const text = formatHttpDebugText({
            kind: 'http',
            message: 'm',
            request: { params: BigInt(5) },
            response: { status: 0 },
        })
        expect(text).toContain('params: 5')
        expect(text).toContain('status: 0')
    })
})

describe('buildHttpDebugInfoFromAxiosError', () => {
    it('handles a null error', () => {
        const info = buildHttpDebugInfoFromAxiosError(null)
        expect(info.message).toBe('Request failed')
        expect(info.request.url).toBeUndefined()
        expect(info.request.method).toBeUndefined()
        expect(info.response).toBeUndefined()
    })

    it('uses HTTP status when there is no message and joins base + relative url', () => {
        const info = buildHttpDebugInfoFromAxiosError({
            config: {
                url: '/api/x',
                baseURL: 'http://host/',
                method: 'get',
                headers: { 'x-trace-id': 'tr', 'X-Request-Id': 'rq', traceparent: 'tp-req' },
                data: { password: 'p' },
            },
            response: {
                status: 404,
                statusText: 'NF',
                headers: { 'X-Trace-Id': 'srv-t', 'X-REQUEST-ID': 'srv-r' },
                data: 'nope',
            },
        })
        expect(info.message).toBe('HTTP 404')
        expect(info.request.url).toBe('http://host/api/x')
        expect(info.request.method).toBe('GET')
        expect(info.request.body).toEqual({ password: '[REDACTED]' })
        expect(info.response).toMatchObject({ status: 404, statusText: 'NF', body: 'nope' })
        expect(info.correlation).toEqual({
            trace_id: 'tr',
            request_id: 'rq',
            traceparent: 'tp-req',
            x_trace_id: 'srv-t',
            x_request_id: 'srv-r',
        })
    })

    it('keeps an absolute url, url without base, and prefers response traceparent', () => {
        const abs = buildHttpDebugInfoFromAxiosError({
            message: 'boom',
            config: { url: 'HTTPS://other/y', baseURL: 'http://host' },
            response: { status: 500, headers: { traceparent: 'tp-resp' } },
        })
        expect(abs.message).toBe('boom')
        expect(abs.request.url).toBe('HTTPS://other/y')
        expect(abs.correlation?.traceparent).toBe('tp-resp')

        const noBase = buildHttpDebugInfoFromAxiosError({ config: { url: 'rel' } })
        expect(noBase.request.url).toBe('rel')
        expect(noBase.correlation).toEqual({
            trace_id: undefined,
            request_id: undefined,
            traceparent: undefined,
            x_trace_id: undefined,
            x_request_id: undefined,
        })
    })
})

describe('correlation header lookup', () => {
    it('uses exact-case keys directly and skips empty values', () => {
        const info = buildHttpDebugInfoFromFetch({
            message: 'm',
            request: { headers: { 'X-Trace-Id': 'direct', 'X-Request-Id': '' } },
        })
        expect(info.correlation?.trace_id).toBe('direct')
        expect(info.correlation?.request_id).toBeUndefined()
    })
})

describe('buildHttpDebugInfoFromFetch', () => {
    it('builds with and without response', () => {
        const withResp = buildHttpDebugInfoFromFetch({
            message: 'm',
            request: { method: 'GET', url: '/u', headers: { Authorization: 'x' }, body: 'b' },
            response: { status: 400, statusText: 'BR', headers: { 'x-request-id': 'r' }, body: { a: 1 } },
        })
        expect(withResp.request.headers).toEqual({ Authorization: '[REDACTED]' })
        expect(withResp.response).toMatchObject({ status: 400, body: { a: 1 } })
        expect(withResp.correlation?.x_request_id).toBe('r')

        const noResp = buildHttpDebugInfoFromFetch({ message: 'm', request: {} })
        expect(noResp.response).toBeUndefined()
    })
})

describe('maybeEmitHttpErrorToast in test mode', () => {
    it('does nothing when IS_TEST is true', () => {
        emitToast.mockClear()
        maybeEmitHttpErrorToast({ kind: 'http', message: 'x', request: {} })
        expect(emitToast).not.toHaveBeenCalled()
    })
})

describe('maybeEmitHttpErrorToast outside test mode', () => {
    let mod: typeof import('./httpDebug')

    beforeEach(async () => {
        emitToast.mockClear()
        vi.resetModules()
        vi.doMock('./env', () => ({ IS_TEST: false }))
        mod = await import('./httpDebug')
    })

    afterEach(() => {
        vi.doUnmock('./env')
    })

    const lastToast = () => emitToast.mock.calls[emitToast.mock.calls.length - 1]?.[0] as { message: string; payload: HttpDebugInfo }

    it('emits HTTP status, method, path and ids for an absolute url', () => {
        mod.maybeEmitHttpErrorToast({
            kind: 'http',
            message: 'a',
            request: { method: 'GET', url: 'http://h/api/x?q=1#frag' },
            response: { status: 500 },
            correlation: { trace_id: 't1', request_id: 'r1' },
        })
        expect(emitToast).toHaveBeenCalledTimes(1)
        expect(lastToast()).toMatchObject({
            kind: 'debug-http-error',
            variant: 'error',
            title: 'Ошибка запроса',
            message: 'HTTP 500 — GET /api/x#frag\ntrace_id=t1 request_id=r1',
            durationMs: 10000,
        })
    })

    it('uses network error label, strips query of relative url, x_* ids fallback', () => {
        mod.maybeEmitHttpErrorToast({
            kind: 'http',
            message: 'b',
            request: { url: '/rel/path?x=1' },
            correlation: { x_trace_id: 'xt', x_request_id: 'xr' },
        })
        expect(lastToast().message).toBe('Ошибка сети —  /rel/path\ntrace_id=xt request_id=xr')
    })

    it('omits the dash part when there is no method and url, and dedupes', () => {
        const info: HttpDebugInfo = { kind: 'http', message: 'c', request: {} }
        mod.maybeEmitHttpErrorToast(info)
        mod.maybeEmitHttpErrorToast(info)
        expect(emitToast).toHaveBeenCalledTimes(1)
        expect(lastToast().message).toBe('Ошибка сети')
    })

    it('handles a relative url that is only a query string', () => {
        mod.maybeEmitHttpErrorToast({ kind: 'http', message: 'd', request: { method: 'PUT', url: '?only' } })
        expect(lastToast().message).toBe('Ошибка сети — PUT ?only')
    })

    it('maybeEmitHttpErrorToastFromAxiosError builds and emits', () => {
        mod.maybeEmitHttpErrorToastFromAxiosError({
            message: 'e',
            config: { method: 'delete', url: 'http://h/z' },
            response: { status: 403 },
        })
        expect(lastToast().message).toBe('HTTP 403 — DELETE /z')
        expect(lastToast().payload.message).toBe('e')
    })
})
