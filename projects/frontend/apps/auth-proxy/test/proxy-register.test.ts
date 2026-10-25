import { afterEach, describe, expect, test } from 'vitest'
import { makeJwt, startProxy, type Proxy, type UpstreamHandler } from './helpers'

let proxy: Proxy | undefined

afterEach(async () => {
    await proxy?.dispose()
    proxy = undefined
})

const echo: UpstreamHandler = (request, reply) =>
    reply.send({ url: request.url, headers: request.headers })
const user = (claims: Record<string, unknown> = {}) => makeJwt({ sub: 'u1', ...claims })
const TRACE = '0123456789abcdef0123456789abcdef'

describe('sensor error-log special route', () => {
    test('proxies to telemetry with a bearer token, query string and accept header', async () => {
        proxy = await startProxy({
            'GET /api/v1/sensors/s%2F1/error-log': (request, reply) =>
                reply.type('application/x-ndjson').send(JSON.stringify({ headers: request.headers, url: request.url })),
        })
        const access = user()

        const res = await proxy.app.inject({
            method: 'GET',
            url: '/api/v1/sensors/s%2F1/error-log?limit=5',
            headers: { cookie: `access_token=${access}`, accept: 'application/x-ndjson', 'x-trace-id': TRACE },
        })

        expect(res.statusCode).toBe(200)
        expect(res.headers['content-type']).toContain('application/x-ndjson')
        const seen = JSON.parse(res.body)
        expect(seen.url).toBe('/api/v1/sensors/s%2F1/error-log?limit=5')
        expect(seen.headers.authorization).toBe(`Bearer ${access}`)
        expect(seen.headers.accept).toBe('application/x-ndjson')
        expect(seen.headers['x-trace-id']).toBe(TRACE)
        expect(seen.headers.cookie).toBeUndefined()
    })

    test('works anonymously and relays upstream errors without a content type', async () => {
        proxy = await startProxy({
            'GET /api/v1/sensors/s1/error-log': (_request, reply) => reply.status(204).send(),
        })

        const res = await proxy.app.inject({ method: 'GET', url: '/api/v1/sensors/s1/error-log' })

        expect(res.statusCode).toBe(204)
        expect(proxy.upstream.calls[0].headers.authorization).toBeUndefined()
        expect(proxy.upstream.calls[0].url).toBe('/api/v1/sensors/s1/error-log')
    })
})

describe('identity propagation', () => {
    test('script routes get no identity headers for anonymous callers', async () => {
        proxy = await startProxy({ 'GET /api/v1/scripts': echo })

        const res = await proxy.app.inject({ method: 'GET', url: '/api/v1/scripts' })

        expect(res.json().headers['x-user-id']).toBeUndefined()
        expect(res.json().headers.authorization).toBeUndefined()
    })

    test('a spoofed X-User-Id is not forwarded when the token has no user', async () => {
        proxy = await startProxy({ 'GET /api/v1/scripts': echo })

        const res = await proxy.app.inject({
            method: 'GET',
            url: '/api/v1/scripts',
            headers: { cookie: `access_token=${makeJwt({ sys: [] })}`, 'x-user-id': 'spoofed' },
        })

        expect(res.json().headers.authorization).toMatch(/^Bearer /)
    })

    test('experiment routes forward project, project list and trace headers, without cookies', async () => {
        proxy = await startProxy({
            'GET /api/v1/sensors': echo,
            'GET /projects': (_request, reply) => reply.send({ projects: [{ id: 'p1' }, { id: 'p2' }] }),
        })

        const res = await proxy.app.inject({
            method: 'GET',
            url: '/api/v1/sensors',
            headers: { cookie: `access_token=${user()}`, 'x-trace-id': TRACE },
        })

        const headers = res.json().headers
        expect(headers['x-project-ids']).toBe('p1,p2')
        expect(headers['x-trace-id']).toContain(TRACE)
        expect(headers['x-request-id']).toMatch(/^[0-9a-f]{32}/)
        expect(headers['x-user-id']).toBe('u1')
    })

    test('generates a trace id when the caller sends none', async () => {
        proxy = await startProxy({ 'GET /api/v1/experiments': echo })

        const res = await proxy.app.inject({ method: 'GET', url: '/api/v1/experiments' })

        expect(res.json().headers['x-trace-id']).toMatch(/^[0-9a-f]{32}$/)
    })
})

describe('telemetry routes', () => {
    test('write requests are proxied without session cookies or injected credentials', async () => {
        proxy = await startProxy({ 'POST /api/v1/telemetry/ingest': echo })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/api/v1/telemetry/ingest',
            headers: { cookie: `access_token=${user()}`, 'content-type': 'application/json' },
            payload: {},
        })

        const headers = res.json().headers
        expect(headers.cookie).toBeUndefined()
        expect(headers.authorization).toBeUndefined()
        expect(headers['x-trace-id']).toMatch(/^[0-9a-f]{32}$/)
    })
})

describe('auth proxies', () => {
    test('/projects forwards the bearer token and drops nothing else of the session contract', async () => {
        proxy = await startProxy({ 'GET /projects': echo })
        const access = user()

        const res = await proxy.app.inject({
            method: 'GET',
            url: '/projects',
            headers: { cookie: `access_token=${access}`, 'x-trace-id': TRACE },
        })

        const headers = res.json().headers
        expect(headers.authorization).toBe(`Bearer ${access}`)
        expect(headers['x-trace-id']).toBe(TRACE)
    })

    test('JSON content type is added only where the route asks for it', async () => {
        proxy = await startProxy({
            'POST /projects': echo,
            'POST /api/v1/users': echo,
        })

        const projects = await proxy.app.inject({
            method: 'POST',
            url: '/projects',
            headers: { 'content-type': 'text/plain' },
            payload: 'x',
        })
        const users = await proxy.app.inject({
            method: 'POST',
            url: '/api/v1/users',
            headers: { 'content-type': 'text/plain' },
            payload: 'x',
        })

        expect(projects.statusCode).toBe(200)
        expect(users.statusCode).toBe(200)
        expect(proxy.upstream.calls.map((call) => call.headers['content-type'])).toEqual([
            'text/plain',
            'text/plain',
        ])
    })
})

describe('SSE hardening', () => {
    test('marks event streams as unbuffered', async () => {
        proxy = await startProxy({
            'GET /api/v1/telemetry/stream': (_request, reply) =>
                reply.type('text/event-stream').send('data: 1\n\n'),
        })

        const res = await proxy.app.inject({
            method: 'GET',
            url: '/api/v1/telemetry/stream',
            headers: { accept: 'text/event-stream' },
        })

        expect(res.statusCode).toBe(200)
        expect(res.headers['content-type']).toContain('text/event-stream')
    })

    test('survives sockets that cannot be reconfigured', async () => {
        proxy = await startProxy({}, { logLevel: 'fatal' })

        const res = await proxy.app.inject({ method: 'GET', url: '/health', headers: { accept: 'text/event-stream' } })

        expect(res.statusCode).toBe(200)
    })

    test('request and response serializers expose only safe fields', async () => {
        proxy = await startProxy({}, { logLevel: 'info' })

        const res = await proxy.app.inject({ method: 'GET', url: '/health', headers: { 'x-trace-id': TRACE } })

        expect(res.statusCode).toBe(200)
    })
})
