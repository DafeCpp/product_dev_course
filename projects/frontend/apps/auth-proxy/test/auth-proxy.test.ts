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
            targetScriptUrl: 'http://example.invalid',
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
        if (res.statusCode !== 200) {
            throw new Error(`SSE proxy failed: ${res.statusCode} ${res.body}`)
        }
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
            targetScriptUrl: 'http://example.invalid',
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
                accept: 'text/event-stream',
            },
        })

        if (res.statusCode !== 200) {
            throw new Error(`SSE proxy failed: ${res.statusCode} ${res.body}`)
        }
        expect(res.headers['content-type']).toContain('text/event-stream')
        expect(res.headers['cache-control']).toContain('no-cache')
        expect(res.headers['x-accel-buffering']).toBe('no')
        expect(res.body).toContain('event: telemetry')
        expect(res.body).toContain('"id":1')

        await app.close()
        await upstream.close()
    })

    test('routes GET /api/v1/sensors/:id/error-log to telemetry-ingest, not experiment-service', async () => {
        const telemetryUpstream = (await import('fastify')).default({ logger: false })
        const seen: Array<{ url: string | undefined; authorization: string | undefined }> = []
        telemetryUpstream.get('/api/v1/sensors/:sensorId/error-log', async (req, reply) => {
            seen.push({
                url: req.url,
                authorization: req.headers.authorization as string | undefined,
            })
            reply.header('content-type', 'application/json')
            return {
                sensor_id: (req.params as { sensorId: string }).sensorId,
                entries: [],
                total: 0,
                limit: 25,
                offset: 0,
            }
        })
        await telemetryUpstream.listen({ port: 0, host: '127.0.0.1' })
        const telemetryAddr = telemetryUpstream.server.address()
        const telemetryPort =
            typeof telemetryAddr === 'object' && telemetryAddr ? telemetryAddr.port : 0

        // experiment-service upstream that would incorrectly return 404 if misrouted
        const experimentUpstream = (await import('fastify')).default({ logger: false })
        experimentUpstream.get('/api/v1/sensors/:sensorId/error-log', async (_req, reply) => {
            reply.status(404).send({ error: 'Not Found' })
        })
        await experimentUpstream.listen({ port: 0, host: '127.0.0.1' })
        const expAddr = experimentUpstream.server.address()
        const experimentPort = typeof expAddr === 'object' && expAddr ? expAddr.port : 0

        const app = await buildServer({
            port: 0,
            targetExperimentUrl: `http://127.0.0.1:${experimentPort}`,
            targetTelemetryUrl: `http://127.0.0.1:${telemetryPort}`,
            targetScriptUrl: 'http://example.invalid',
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

        try {
            const res = await app.inject({
                method: 'GET',
                url: '/api/v1/sensors/1a9d0362-815e-4683-94b1-2767dfa501f5/error-log?limit=25&offset=0&project_id=3739a924-37e1-402c-b9c7-fbf27f118ded',
                headers: {
                    cookie: 'access_token=fake.jwt.token',
                },
            })

            expect(res.statusCode).toBe(200)
            expect(res.headers['content-type']).toContain('application/json')
            const body = res.json()
            expect(body.sensor_id).toBe('1a9d0362-815e-4683-94b1-2767dfa501f5')
            expect(body.entries).toEqual([])

            expect(seen).toHaveLength(1)
            expect(seen[0].url).toContain('limit=25')
            expect(seen[0].url).toContain('offset=0')
            expect(seen[0].url).toContain('project_id=3739a924-37e1-402c-b9c7-fbf27f118ded')
            expect(seen[0].authorization).toBe('Bearer fake.jwt.token')
        } finally {
            await app.close()
            await telemetryUpstream.close()
            await experimentUpstream.close()
        }
    })

    test('blocks state-changing /api requests with session cookie but without CSRF header', async () => {
        const upstream = (await import('fastify')).default({ logger: false })
        upstream.post('/api/v1/ping', async () => ({ ok: true }))
        await upstream.listen({ port: 0, host: '127.0.0.1' })
        const address = upstream.server.address()
        const port = typeof address === 'object' && address ? address.port : 0

        const app = await buildServer({
            port: 0,
            targetExperimentUrl: `http://127.0.0.1:${port}`,
            targetTelemetryUrl: 'http://example.invalid',
            targetScriptUrl: 'http://example.invalid',
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

        try {
            const res = await app.inject({
                method: 'POST',
                url: '/api/v1/ping',
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: 'access_token=a.b.c; csrf_token=csrf123',
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ x: 1 }),
            })

            expect(res.statusCode).toBe(403)
            expect(res.json()).toEqual({ error: 'CSRF token missing or invalid' })
        } finally {
            await app.close()
            await upstream.close()
        }
    })

    test('allows state-changing /api requests when CSRF header matches cookie', async () => {
        const upstream = (await import('fastify')).default({ logger: false })
        upstream.post('/api/v1/ping', async () => ({ ok: true }))
        await upstream.listen({ port: 0, host: '127.0.0.1' })
        const address = upstream.server.address()
        const port = typeof address === 'object' && address ? address.port : 0

        const app = await buildServer({
            port: 0,
            targetExperimentUrl: `http://127.0.0.1:${port}`,
            targetTelemetryUrl: 'http://example.invalid',
            targetScriptUrl: 'http://example.invalid',
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
        try {
            const res = await app.inject({
                method: 'POST',
                url: '/api/v1/ping',
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: 'access_token=a.b.c; csrf_token=csrf123',
                    'x-csrf-token': 'csrf123',
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ x: 1 }),
            })

            expect(res.statusCode).toBe(200)
            expect(res.json()).toEqual({ ok: true })
        } finally {
            await app.close()
            await upstream.close()
        }
    })

    test('blocks state-changing /api requests with invalid origin', async () => {
        const upstream = (await import('fastify')).default({ logger: false })
        upstream.post('/api/v1/ping', async () => ({ ok: true }))
        await upstream.listen({ port: 0, host: '127.0.0.1' })
        const address = upstream.server.address()
        const port = typeof address === 'object' && address ? address.port : 0

        const app = await buildServer({
            port: 0,
            targetExperimentUrl: `http://127.0.0.1:${port}`,
            targetTelemetryUrl: 'http://example.invalid',
            targetScriptUrl: 'http://example.invalid',
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
        try {
            const res = await app.inject({
                method: 'POST',
                url: '/api/v1/ping',
                headers: {
                    origin: 'http://evil.example',
                    cookie: 'access_token=a.b.c; csrf_token=csrf123',
                    'x-csrf-token': 'csrf123',
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ x: 1 }),
            })

            expect(res.statusCode).toBe(403)
            expect(res.json()).toEqual({ error: 'CSRF origin missing or invalid' })
        } finally {
            await app.close()
            await upstream.close()
        }
    })

    test('allows state-changing /api requests when referer origin is allowed', async () => {
        const upstream = (await import('fastify')).default({ logger: false })
        upstream.post('/api/v1/ping', async () => ({ ok: true }))
        await upstream.listen({ port: 0, host: '127.0.0.1' })
        const address = upstream.server.address()
        const port = typeof address === 'object' && address ? address.port : 0

        const app = await buildServer({
            port: 0,
            targetExperimentUrl: `http://127.0.0.1:${port}`,
            targetTelemetryUrl: 'http://example.invalid',
            targetScriptUrl: 'http://example.invalid',
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
        try {
            const res = await app.inject({
                method: 'POST',
                url: '/api/v1/ping',
                headers: {
                    referer: 'http://localhost:3000/some/page',
                    cookie: 'access_token=a.b.c; csrf_token=csrf123',
                    'x-csrf-token': 'csrf123',
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ x: 1 }),
            })

            expect(res.statusCode).toBe(200)
            expect(res.json()).toEqual({ ok: true })
        } finally {
            await app.close()
            await upstream.close()
        }
    })

    test('blocks state-changing /api requests when origin is missing', async () => {
        const upstream = (await import('fastify')).default({ logger: false })
        upstream.post('/api/v1/ping', async () => ({ ok: true }))
        await upstream.listen({ port: 0, host: '127.0.0.1' })
        const address = upstream.server.address()
        const port = typeof address === 'object' && address ? address.port : 0

        const app = await buildServer({
            port: 0,
            targetExperimentUrl: `http://127.0.0.1:${port}`,
            targetTelemetryUrl: 'http://example.invalid',
            targetScriptUrl: 'http://example.invalid',
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
        try {
            const res = await app.inject({
                method: 'POST',
                url: '/api/v1/ping',
                headers: {
                    cookie: 'access_token=a.b.c; csrf_token=csrf123',
                    'x-csrf-token': 'csrf123',
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ x: 1 }),
            })

            expect(res.statusCode).toBe(403)
            expect(res.json()).toEqual({ error: 'CSRF origin missing or invalid' })
        } finally {
            await app.close()
            await upstream.close()
        }
    })
})

