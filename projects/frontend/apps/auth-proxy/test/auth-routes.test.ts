import { afterEach, describe, expect, test } from 'vitest'
import {
    cookieNamed,
    sessionHeaders,
    startProxy,
    type Proxy,
    type UpstreamHandler,
} from './helpers'

const tokens = { access_token: 'new-access', refresh_token: 'new-refresh', id: 'user-1' }

let proxy: Proxy | undefined

afterEach(async () => {
    await proxy?.dispose()
    proxy = undefined
})

const json = (status: number, body: unknown): UpstreamHandler => (_request, reply) =>
    reply.status(status).send(body)
const text = (status: number): UpstreamHandler => (_request, reply) =>
    reply.status(status).type('text/plain').send('not json')

describe.each(['login', 'register'])('POST /auth/%s', (action) => {
    const url = `/auth/${action}`

    test('sets session cookies and strips tokens from the body', async () => {
        proxy = await startProxy({ [`POST ${url}`]: json(200, tokens) })

        const res = await proxy.app.inject({ method: 'POST', url, payload: { email: 'a@b.c' } })

        expect(res.statusCode).toBe(200)
        expect(res.json()).toEqual({ id: 'user-1' })
        expect(cookieNamed(res, 'access_token')).toContain('new-access')
        expect(cookieNamed(res, 'refresh_token')).toContain('new-refresh')
        expect(cookieNamed(res, 'csrf_token')).toBeDefined()
        expect(proxy.upstream.calls[0].body).toEqual({ email: 'a@b.c' })
    })

    test('forwards upstream error status and body without cookies', async () => {
        proxy = await startProxy({ [`POST ${url}`]: json(401, { detail: 'bad credentials' }) })

        const res = await proxy.app.inject({ method: 'POST', url, payload: {} })

        expect(res.statusCode).toBe(401)
        expect(res.json()).toEqual({ detail: 'bad credentials' })
        expect(res.headers['set-cookie']).toBeUndefined()
    })

    test('falls back to an empty object when the error body is not JSON', async () => {
        proxy = await startProxy({ [`POST ${url}`]: text(500) })

        const res = await proxy.app.inject({ method: 'POST', url })

        expect(res.statusCode).toBe(500)
        expect(res.json()).toEqual({})
    })

    test('answers 502 when auth-service omits the access token', async () => {
        proxy = await startProxy({ [`POST ${url}`]: json(200, { id: 'user-1' }) })

        const res = await proxy.app.inject({ method: 'POST', url, payload: {} })

        expect(res.statusCode).toBe(502)
        expect(res.json()).toEqual({ error: 'Auth service response missing access_token' })
        expect(res.headers['set-cookie']).toBeUndefined()
    })
})

describe('POST /auth/login password_change_required', () => {
    test('is forwarded when true and dropped when false', async () => {
        let required = true
        proxy = await startProxy({
            'POST /auth/login': (_request, reply) =>
                reply.send({ ...tokens, password_change_required: required }),
        })

        const forwarded = await proxy.app.inject({ method: 'POST', url: '/auth/login', payload: {} })
        required = false
        const dropped = await proxy.app.inject({ method: 'POST', url: '/auth/login', payload: {} })

        expect(forwarded.json()).toEqual({ id: 'user-1', password_change_required: true })
        expect(dropped.json()).toEqual({ id: 'user-1' })
    })
})

describe('POST /auth/refresh', () => {
    test('rejects requests without a refresh token', async () => {
        proxy = await startProxy({})

        const res = await proxy.app.inject({ method: 'POST', url: '/auth/refresh' })

        expect(res.statusCode).toBe(401)
        expect(res.json()).toEqual({ error: 'Refresh token not provided' })
        expect(proxy.upstream.calls).toHaveLength(0)
    })

    test('rotates cookies using the refresh cookie', async () => {
        proxy = await startProxy({ 'POST /auth/refresh': json(200, tokens) })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/refresh',
            headers: { cookie: 'refresh_token=old-refresh' },
        })

        expect(res.statusCode).toBe(200)
        expect(res.json()).toEqual({ id: 'user-1' })
        expect(proxy.upstream.calls[0].body).toEqual({ refresh_token: 'old-refresh' })
        expect(cookieNamed(res, 'access_token')).toContain('new-access')
    })

    test('accepts the refresh token from the JSON body', async () => {
        proxy = await startProxy({ 'POST /auth/refresh': json(200, tokens) })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/refresh',
            payload: { refresh_token: 'body-refresh' },
        })

        expect(res.statusCode).toBe(200)
        expect(proxy.upstream.calls[0].body).toEqual({ refresh_token: 'body-refresh' })
    })

    test('clears session cookies when auth-service rejects the token', async () => {
        proxy = await startProxy({ 'POST /auth/refresh': json(401, { detail: 'expired' }) })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/refresh',
            headers: { cookie: 'refresh_token=stale' },
        })

        expect(res.statusCode).toBe(401)
        expect(res.json()).toEqual({ detail: 'expired' })
        expect(cookieNamed(res, 'access_token')).toContain('Expires=')
        expect(cookieNamed(res, 'refresh_token')).toContain('Expires=')
        expect(cookieNamed(res, 'csrf_token')).toContain('Expires=')
    })

    test('answers 502 when the refreshed session has no access token', async () => {
        proxy = await startProxy({ 'POST /auth/refresh': json(200, {}) })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/refresh',
            headers: { cookie: 'refresh_token=old' },
        })

        expect(res.statusCode).toBe(502)
    })
})

describe('POST /auth/logout', () => {
    test('revokes the refresh token upstream and clears cookies', async () => {
        proxy = await startProxy({ 'POST /auth/logout': json(200, {}) })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/logout',
            headers: sessionHeaders('refresh_token=to-revoke'),
            payload: {},
        })

        expect(res.statusCode).toBe(200)
        expect(res.json()).toEqual({ ok: true })
        expect(proxy.upstream.calls[0].body).toEqual({ refresh_token: 'to-revoke' })
        expect(cookieNamed(res, 'refresh_token')).toContain('Expires=')
    })

    test('sends an empty body when there is no refresh cookie', async () => {
        proxy = await startProxy({ 'POST /auth/logout': json(200, {}) })

        const res = await proxy.app.inject({ method: 'POST', url: '/auth/logout' })

        expect(res.json()).toEqual({ ok: true })
        expect(proxy.upstream.calls[0].body).toEqual({})
    })

    test('still logs the user out when auth-service is unreachable', async () => {
        proxy = await startProxy({}, { authUrl: 'http://127.0.0.1:1' })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/logout',
            headers: sessionHeaders('refresh_token=to-revoke'),
            payload: {},
        })

        expect(res.statusCode).toBe(200)
        expect(res.json()).toEqual({ ok: true })
        expect(cookieNamed(res, 'access_token')).toContain('Expires=')
    })
})

describe('password reset', () => {
    test('request relays the upstream status and body', async () => {
        proxy = await startProxy({ 'POST /auth/password-reset/request': json(202, { sent: true }) })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/password-reset/request',
            payload: { email: 'a@b.c' },
        })

        expect(res.statusCode).toBe(202)
        expect(res.json()).toEqual({ sent: true })
    })

    test('request tolerates a non-JSON upstream body', async () => {
        proxy = await startProxy({ 'POST /auth/password-reset/request': text(503) })

        const res = await proxy.app.inject({ method: 'POST', url: '/auth/password-reset/request' })

        expect(res.statusCode).toBe(503)
        expect(res.json()).toEqual({})
    })

    test('confirm signs the user in when auth-service returns tokens', async () => {
        proxy = await startProxy({ 'POST /auth/password-reset/confirm': json(200, tokens) })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/password-reset/confirm',
            payload: { token: 't', new_password: 'p' },
        })

        expect(res.json()).toEqual({ id: 'user-1' })
        expect(cookieNamed(res, 'access_token')).toContain('new-access')
    })

    test('confirm returns the body as is when there are no tokens', async () => {
        proxy = await startProxy({ 'POST /auth/password-reset/confirm': json(200, { ok: true }) })

        const res = await proxy.app.inject({ method: 'POST', url: '/auth/password-reset/confirm' })

        expect(res.json()).toEqual({ ok: true })
        expect(res.headers['set-cookie']).toBeUndefined()
    })

    test('confirm forwards upstream errors', async () => {
        proxy = await startProxy({ 'POST /auth/password-reset/confirm': json(400, { detail: 'bad token' }) })

        const res = await proxy.app.inject({ method: 'POST', url: '/auth/password-reset/confirm' })

        expect(res.statusCode).toBe(400)
        expect(res.json()).toEqual({ detail: 'bad token' })
    })

    test('confirm tolerates a non-JSON error body', async () => {
        proxy = await startProxy({ 'POST /auth/password-reset/confirm': text(500) })

        const res = await proxy.app.inject({ method: 'POST', url: '/auth/password-reset/confirm' })

        expect(res.statusCode).toBe(500)
        expect(res.json()).toEqual({})
    })
})

describe('POST /auth/change-password failure paths', () => {
    const body = { old_password: 'a', new_password: 'b' }

    test('answers 401 when there is neither an access nor a refresh token', async () => {
        proxy = await startProxy({})

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/change-password',
            payload: body,
        })

        expect(res.statusCode).toBe(401)
        expect(res.json()).toEqual({ error: 'Unauthorized' })
        expect(res.headers['set-cookie']).toBeUndefined()
    })

    test('clears cookies when the refresh token is rejected', async () => {
        proxy = await startProxy({ 'POST /auth/refresh': json(401, { detail: 'revoked' }) })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/change-password',
            headers: sessionHeaders('refresh_token=revoked'),
            payload: body,
        })

        expect(res.statusCode).toBe(401)
        expect(res.json()).toEqual({ detail: 'revoked' })
        expect(cookieNamed(res, 'refresh_token')).toContain('Expires=')
    })

    test('tolerates a non-JSON refresh failure', async () => {
        proxy = await startProxy({ 'POST /auth/refresh': text(500) })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/change-password',
            headers: sessionHeaders('refresh_token=any'),
            payload: body,
        })

        expect(res.statusCode).toBe(500)
        expect(res.json()).toEqual({})
    })

    test('answers 502 when the refresh response has no access token', async () => {
        proxy = await startProxy({ 'POST /auth/refresh': json(200, {}) })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/change-password',
            headers: sessionHeaders('refresh_token=any'),
            payload: body,
        })

        expect(res.statusCode).toBe(502)
        expect(res.headers['set-cookie']).toBeUndefined()
    })

    test('keeps refreshed cookies when the password change itself is rejected', async () => {
        proxy = await startProxy({
            'POST /auth/refresh': json(200, tokens),
            'POST /auth/change-password': json(400, { detail: 'weak password' }),
        })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/change-password',
            headers: sessionHeaders('refresh_token=ok'),
            payload: body,
        })

        expect(res.statusCode).toBe(400)
        expect(res.json()).toEqual({ detail: 'weak password' })
        expect(cookieNamed(res, 'access_token')).toContain('new-access')
    })

    test('tolerates a non-JSON change-password error', async () => {
        proxy = await startProxy({ 'POST /auth/change-password': text(500) })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/change-password',
            headers: sessionHeaders('access_token=live'),
            payload: body,
        })

        expect(res.statusCode).toBe(500)
        expect(res.json()).toEqual({})
    })

    test('answers 502 and keeps refreshed cookies when the new session has no access token', async () => {
        proxy = await startProxy({
            'POST /auth/refresh': json(200, tokens),
            'POST /auth/change-password': json(200, { id: 'user-1' }),
        })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/change-password',
            headers: sessionHeaders('refresh_token=ok'),
            payload: body,
        })

        expect(res.statusCode).toBe(502)
        expect(cookieNamed(res, 'access_token')).toContain('new-access')
    })

    test('answers 502 without cookies when nothing was refreshed', async () => {
        proxy = await startProxy({ 'POST /auth/change-password': json(200, { id: 'user-1' }) })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/change-password',
            headers: sessionHeaders('access_token=live'),
            payload: body,
        })

        expect(res.statusCode).toBe(502)
        expect(res.headers['set-cookie']).toBeUndefined()
    })

    test('clears cookies when the retry refresh after a stale token fails', async () => {
        proxy = await startProxy({
            'POST /auth/change-password': json(401, { detail: 'stale' }),
            'POST /auth/refresh': json(401, { detail: 'revoked' }),
        })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/auth/change-password',
            headers: sessionHeaders('access_token=stale; refresh_token=revoked'),
            payload: body,
        })

        expect(res.statusCode).toBe(401)
        expect(cookieNamed(res, 'access_token')).toContain('Expires=')
    })
})

describe('GET /auth/me', () => {
    test('requires an access cookie', async () => {
        proxy = await startProxy({})

        const res = await proxy.app.inject({ method: 'GET', url: '/auth/me' })

        expect(res.statusCode).toBe(401)
        expect(res.json()).toEqual({ error: 'Unauthorized' })
    })

    test('returns the profile and forwards the bearer token', async () => {
        proxy = await startProxy({ 'GET /auth/me': json(200, { id: 'user-1' }) })

        const res = await proxy.app.inject({
            method: 'GET',
            url: '/auth/me',
            headers: { cookie: 'access_token=live' },
        })

        expect(res.json()).toEqual({ id: 'user-1' })
        expect(proxy.upstream.calls[0].headers.authorization).toBe('Bearer live')
    })

    test('clears cookies when the token is rejected', async () => {
        proxy = await startProxy({ 'GET /auth/me': json(401, { detail: 'expired' }) })

        const res = await proxy.app.inject({
            method: 'GET',
            url: '/auth/me',
            headers: { cookie: 'access_token=expired' },
        })

        expect(res.statusCode).toBe(401)
        expect(cookieNamed(res, 'access_token')).toContain('Expires=')
    })

    test('keeps cookies on other upstream errors', async () => {
        proxy = await startProxy({ 'GET /auth/me': text(503) })

        const res = await proxy.app.inject({
            method: 'GET',
            url: '/auth/me',
            headers: { cookie: 'access_token=live' },
        })

        expect(res.statusCode).toBe(503)
        expect(res.json()).toEqual({})
        expect(res.headers['set-cookie']).toBeUndefined()
    })

    test('tolerates a non-JSON success body', async () => {
        proxy = await startProxy({ 'GET /auth/me': text(200) })

        const res = await proxy.app.inject({
            method: 'GET',
            url: '/auth/me',
            headers: { cookie: 'access_token=live' },
        })

        expect(res.statusCode).toBe(200)
        expect(res.json()).toEqual({})
    })
})
