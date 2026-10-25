import { afterEach, describe, expect, test } from 'vitest'
import { makeJwt, sessionHeaders, startProxy, type Proxy } from './helpers'

let proxy: Proxy | undefined

afterEach(async () => {
    await proxy?.dispose()
    proxy = undefined
})

const ok = (_request: unknown, reply: { send: (body: unknown) => unknown }) => reply.send({ ok: true })
const nowSec = () => Math.floor(Date.now() / 1000)

describe('CSRF protection for state-changing requests', () => {
    const route = { 'POST /api/v1/experiments': ok }

    test('lets anonymous requests through (no session to protect)', async () => {
        proxy = await startProxy(route)

        const res = await proxy.app.inject({ method: 'POST', url: '/api/v1/experiments', payload: {} })

        expect(res.statusCode).toBe(200)
    })

    test('exempts login and telemetry ingestion even with session cookies', async () => {
        proxy = await startProxy({
            'POST /auth/login': (_request, reply) => reply.status(401).send({}),
            'POST /api/v1/telemetry/ingest': ok,
        })
        const headers = { cookie: 'access_token=live', 'content-type': 'application/json' }

        const login = await proxy.app.inject({ method: 'POST', url: '/auth/login', headers, payload: {} })
        const ingest = await proxy.app.inject({
            method: 'POST',
            url: '/api/v1/telemetry/ingest',
            headers,
            payload: {},
        })

        expect(login.statusCode).toBe(401)
        expect(ingest.statusCode).toBe(200)
    })

    test('accepts a valid session with matching origin and token', async () => {
        proxy = await startProxy(route)

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/api/v1/experiments',
            headers: sessionHeaders('access_token=live'),
            payload: {},
        })

        expect(res.statusCode).toBe(200)
    })

    test('accepts the origin from the Referer header, case-insensitively', async () => {
        proxy = await startProxy(route)

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/api/v1/experiments',
            headers: sessionHeaders('access_token=live', {
                origin: undefined,
                referer: 'http://LOCALHOST:3000/experiments/1',
            }),
            payload: {},
        })

        expect(res.statusCode).toBe(200)
    })

    test.each([
        ['an unknown origin', { origin: 'http://evil.example' }],
        ['a malformed referer without origin', { origin: undefined, referer: 'not a url' }],
        ['no origin and no referer', { origin: undefined }],
    ])('rejects %s', async (_name, extra) => {
        proxy = await startProxy(route)

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/api/v1/experiments',
            headers: sessionHeaders('access_token=live', extra),
            payload: {},
        })

        expect(res.statusCode).toBe(403)
        expect(res.json()).toEqual({ error: 'CSRF origin missing or invalid' })
        expect(proxy.upstream.calls).toHaveLength(0)
    })

    test.each([
        ['a missing header', { 'x-csrf-token': undefined }],
        ['a mismatching header', { 'x-csrf-token': 'other' }],
    ])('rejects %s', async (_name, extra) => {
        proxy = await startProxy(route)

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/api/v1/experiments',
            headers: sessionHeaders('refresh_token=live', extra),
            payload: {},
        })

        expect(res.statusCode).toBe(403)
        expect(res.json()).toEqual({ error: 'CSRF token missing or invalid' })
    })

    test('rejects a request whose csrf cookie is missing', async () => {
        proxy = await startProxy(route)

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/api/v1/experiments',
            headers: {
                origin: 'http://localhost:3000',
                cookie: 'access_token=live',
                'x-csrf-token': 'anything',
                'content-type': 'application/json',
            },
            payload: {},
        })

        expect(res.statusCode).toBe(403)
    })
})

describe('telemetry read authorization', () => {
    const route = {
        'GET /api/v1/telemetry/query': (request: { headers: Record<string, unknown> }, reply: { send: (b: unknown) => unknown }) =>
            reply.send({ authorization: request.headers.authorization ?? null, cookie: request.headers.cookie ?? null }),
    }

    const query = (cookie?: string, extra: Record<string, string> = {}) => ({
        method: 'GET' as const,
        url: '/api/v1/telemetry/query',
        headers: { ...(cookie ? { cookie } : {}), ...extra },
    })

    test('keeps an explicit Authorization header', async () => {
        proxy = await startProxy(route)

        const res = await proxy.app.inject(query('access_token=cookie', { authorization: 'Bearer explicit' }))

        expect(res.json().authorization).toBe('Bearer explicit')
    })

    test('uses a fresh access cookie and never leaks cookies upstream', async () => {
        proxy = await startProxy(route)
        const access = makeJwt({ sub: 'u1', exp: nowSec() + 3600 })

        const res = await proxy.app.inject(query(`access_token=${access}`))

        expect(res.json()).toEqual({ authorization: `Bearer ${access}`, cookie: null })
    })

    test('stays anonymous without any session', async () => {
        proxy = await startProxy(route)

        const res = await proxy.app.inject(query())

        expect(res.json().authorization).toBeNull()
    })

    test('refreshes an expired access token with the refresh cookie', async () => {
        proxy = await startProxy({
            ...route,
            'POST /auth/refresh': (_request, reply) =>
                reply.send({ access_token: 'refreshed', refresh_token: 'refreshed-refresh' }),
        })
        const expired = makeJwt({ sub: 'u1', exp: nowSec() - 60 })

        const res = await proxy.app.inject(query(`access_token=${expired}; refresh_token=old`))

        expect(res.json().authorization).toBe('Bearer refreshed')
        expect(proxy.upstream.calls.find((call) => call.url === '/auth/refresh')?.body).toEqual({
            refresh_token: 'old',
        })
    })

    test('refreshes using only the refresh cookie when there is no access cookie', async () => {
        proxy = await startProxy({
            ...route,
            'POST /auth/refresh': (_request, reply) => reply.send({ access_token: 'from-refresh' }),
        })

        const res = await proxy.app.inject(query('refresh_token=old'))

        expect(res.json().authorization).toBe('Bearer from-refresh')
    })

    test('falls back to the expired access token when refresh is rejected', async () => {
        proxy = await startProxy({
            ...route,
            'POST /auth/refresh': (_request, reply) => reply.status(401).send({}),
        })
        const expired = makeJwt({ sub: 'u1', exp: nowSec() - 60 })

        const res = await proxy.app.inject(query(`access_token=${expired}; refresh_token=revoked`))

        expect(res.json().authorization).toBe(`Bearer ${expired}`)
    })

    test('falls back to the expired access token when refresh returns no access token', async () => {
        proxy = await startProxy({
            ...route,
            'POST /auth/refresh': (_request, reply) => reply.send({}),
        })
        const expired = makeJwt({ sub: 'u1', exp: nowSec() - 60 })

        const res = await proxy.app.inject(query(`access_token=${expired}; refresh_token=any`))

        expect(res.json().authorization).toBe(`Bearer ${expired}`)
    })

    test('falls back to the expired access token when there is no refresh cookie', async () => {
        proxy = await startProxy(route)
        const expired = makeJwt({ sub: 'u1', exp: nowSec() - 60 })

        const res = await proxy.app.inject(query(`access_token=${expired}`))

        expect(res.json().authorization).toBe(`Bearer ${expired}`)
        expect(proxy.upstream.calls.some((call) => call.url === '/auth/refresh')).toBe(false)
    })
})
