import fastify from 'fastify'
import { buildServer, type Config } from '../src/index'

function getSetCookies(res: { headers: { 'set-cookie'?: unknown } }): string[] {
    const setCookie = res.headers['set-cookie']
    if (!setCookie) return []
    if (Array.isArray(setCookie)) return setCookie.filter((value): value is string => typeof value === 'string')
    return typeof setCookie === 'string' ? [setCookie] : []
}

function cookieValue(setCookieHeader: string, name: string): string | undefined {
    const firstPart = setCookieHeader.split(';')[0]
    const separator = firstPart.indexOf('=')
    if (separator === -1) return undefined
    return firstPart.slice(0, separator) === name
        ? firstPart.slice(separator + 1)
        : undefined
}

function config(authUrl: string): Config {
    return {
        port: 0,
        targetExperimentUrl: 'http://example.invalid',
        targetTelemetryUrl: 'http://example.invalid',
        targetConfigServiceUrl: 'http://example.invalid',
        targetScriptUrl: 'http://example.invalid',
        authUrl,
        corsOrigins: ['http://localhost:3000'],
        cookieSecure: false,
        cookieSameSite: 'lax',
        accessCookieName: 'access_token',
        refreshCookieName: 'refresh_token',
        accessTtlSec: 900,
        refreshTtlSec: 1_209_600,
        rateLimitWindowMs: 60_000,
        rateLimitMax: 60,
        logLevel: 'silent',
    }
}

describe('POST /auth/change-password', () => {
    const oldAccessToken = 'old-access-token'
    const csrfToken = 'csrf-token'

    async function createProxy(upstream: ReturnType<typeof fastify>) {
        await upstream.listen({ port: 0, host: '127.0.0.1' })
        const address = upstream.server.address()
        const port = typeof address === 'object' && address ? address.port : 0
        const app = await buildServer(config(`http://127.0.0.1:${port}`))
        await app.ready()
        return app
    }

    function authenticatedHeaders() {
        return {
            origin: 'http://localhost:3000',
            cookie: `access_token=${oldAccessToken}; csrf_token=${csrfToken}`,
            'x-csrf-token': csrfToken,
            'content-type': 'application/json',
        }
    }

    test('forwards the authenticated request and rotates session cookies', async () => {
        const upstream = fastify({ logger: false })
        let receivedAuthorization: string | undefined
        let receivedBody: unknown
        upstream.post('/auth/change-password', async (request) => {
            receivedAuthorization = request.headers.authorization
            receivedBody = request.body
            return {
                id: 'user-1',
                email: 'user@example.com',
                access_token: 'new-access-token',
                refresh_token: 'new-refresh-token',
            }
        })
        const app = await createProxy(upstream)

        try {
            const res = await app.inject({
                method: 'POST',
                url: '/auth/change-password',
                headers: authenticatedHeaders(),
                payload: JSON.stringify({ old_password: 'old-password', new_password: 'new-password' }),
            })

            expect(res.statusCode).toBe(200)
            expect(receivedAuthorization).toBe(`Bearer ${oldAccessToken}`)
            expect(receivedBody).toEqual({ old_password: 'old-password', new_password: 'new-password' })
            expect(res.json()).toEqual({ id: 'user-1', email: 'user@example.com' })

            const cookies = getSetCookies(res)
            expect(cookies.map((value) => cookieValue(value, 'access_token'))).toContain('new-access-token')
            expect(cookies.map((value) => cookieValue(value, 'refresh_token'))).toContain('new-refresh-token')
            expect(cookies.some((value) => value.startsWith('csrf_token='))).toBe(true)
        } finally {
            await app.close()
            await upstream.close()
        }
    })

    test('requires CSRF validation before reaching auth-service', async () => {
        const upstream = fastify({ logger: false })
        let called = false
        upstream.post('/auth/change-password', async () => {
            called = true
            return { access_token: 'new-access-token' }
        })
        const app = await createProxy(upstream)

        try {
            const res = await app.inject({
                method: 'POST',
                url: '/auth/change-password',
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: `access_token=${oldAccessToken}; csrf_token=${csrfToken}`,
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ old_password: 'old-password', new_password: 'new-password' }),
            })

            expect(res.statusCode).toBe(403)
            expect(res.json()).toEqual({ error: 'CSRF token missing or invalid' })
            expect(called).toBe(false)
        } finally {
            await app.close()
            await upstream.close()
        }
    })

    test('rejects requests without an access-token cookie', async () => {
        const upstream = fastify({ logger: false })
        const app = await createProxy(upstream)

        try {
            const res = await app.inject({
                method: 'POST',
                url: '/auth/change-password',
                headers: { 'content-type': 'application/json' },
                payload: JSON.stringify({ old_password: 'old-password', new_password: 'new-password' }),
            })

            expect(res.statusCode).toBe(401)
            expect(res.json()).toEqual({ error: 'Unauthorized' })
        } finally {
            await app.close()
            await upstream.close()
        }
    })

    test('preserves auth-service errors without replacing session cookies', async () => {
        const upstream = fastify({ logger: false })
        upstream.post('/auth/change-password', async (_request, reply) => {
            reply.status(400)
            return { error: 'Invalid current password' }
        })
        const app = await createProxy(upstream)

        try {
            const res = await app.inject({
                method: 'POST',
                url: '/auth/change-password',
                headers: authenticatedHeaders(),
                payload: JSON.stringify({ old_password: 'wrong-password', new_password: 'new-password' }),
            })

            expect(res.statusCode).toBe(400)
            expect(res.json()).toEqual({ error: 'Invalid current password' })
            expect(getSetCookies(res)).toEqual([])
        } finally {
            await app.close()
            await upstream.close()
        }
    })
})
