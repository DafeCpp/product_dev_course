import { buildServer, getOutgoingRequestHeaders, parseCookies } from '../src/index'

describe('auth-proxy helpers', () => {
    test('parseCookies parses a Cookie header', () => {
        expect(parseCookies('a=1; b=hello%20world; c=%7B%22x%22%3A1%7D')).toEqual({
            a: '1',
            b: 'hello world',
            c: '{"x":1}',
        })
    })

    test('getOutgoingRequestHeaders preserves trace id and generates request id', () => {
        const h = getOutgoingRequestHeaders('trace-123')
        expect(h['X-Trace-Id']).toBe('trace-123')
        expect(typeof h['X-Request-Id']).toBe('string')
        expect(h['X-Request-Id'].length).toBeGreaterThan(0)
    })
})

describe('auth-proxy server', () => {
    test('GET /health returns ok', async () => {
        const app = await buildServer({
            port: 0,
            targetExperimentUrl: 'http://example.invalid',
            authUrl: 'http://example.invalid',
            corsOrigins: ['http://localhost:3000'],
            cookieSecure: false,
            cookieSameSite: 'lax',
            accessCookieName: 'access_token',
            refreshCookieName: 'refresh_token',
            accessTtlSec: 900,
            refreshTtlSec: 1209600,
            rateLimitWindowMs: 60000,
            rateLimitMax: 60,
            logLevel: 'silent',
        })

        await app.ready()
        const res = await app.inject({ method: 'GET', url: '/health' })
        expect(res.statusCode).toBe(200)
        expect(res.json()).toEqual({ status: 'ok' })
        await app.close()
    })
})

