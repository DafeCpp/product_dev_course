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
            targetTelemetryUrl: 'http://example.invalid',
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

    test('proxies telemetry SSE stream via /api/v1/telemetry/stream', async () => {
        const upstream = (await import('fastify')).default({ logger: false })
        upstream.get('/api/v1/telemetry/stream', async (_req, reply) => {
            reply.header('Content-Type', 'text/event-stream')
            return 'event: telemetry\ndata: {"id":1}\n\n'
        })
        await upstream.listen({ port: 0, host: '127.0.0.1' })
        const address = upstream.server.address()
        const port = typeof address === 'object' && address ? address.port : 0

        const app = await buildServer({
            port: 0,
            targetExperimentUrl: 'http://example.invalid',
            targetTelemetryUrl: `http://127.0.0.1:${port}`,
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

        const res = await app.inject({
            method: 'GET',
            url: '/api/v1/telemetry/stream?sensor_id=00000000-0000-0000-0000-000000000000',
            headers: {
                authorization: 'Bearer sensor-token',
            },
        })

        expect(res.statusCode).toBe(200)
        expect(res.headers['content-type']).toContain('text/event-stream')
        expect(res.body).toContain('event: telemetry')
        expect(res.body).toContain('"id":1')

        await app.close()
        await upstream.close()
    })
})

