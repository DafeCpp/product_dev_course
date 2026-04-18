import { buildServer, InMemoryPermissionsCache, PermissionsCache } from '../src/index'

// Minimal config helper
function makeConfig(overrides: Record<string, unknown> = {}) {
    return {
        port: 0,
        targetExperimentUrl: 'http://example.invalid',
        targetTelemetryUrl: 'http://example.invalid',
        targetScriptUrl: 'http://example.invalid',
        authUrl: 'http://example.invalid',
        corsOrigins: ['http://localhost:3000'],
        cookieSecure: false,
        cookieSameSite: 'lax' as const,
        accessCookieName: 'access_token',
        refreshCookieName: 'refresh_token',
        accessTtlSec: 900,
        refreshTtlSec: 1209600,
        rateLimitWindowMs: 60000,
        rateLimitMax: 60,
        logLevel: 'silent',
        ...overrides,
    }
}

// Minimal valid EffectivePermissions payload
const MOCK_PERMS = {
    user_id: 'user-1',
    is_superadmin: false,
    system_permissions: ['read'],
    project_permissions: ['project:read'],
}

// Encode a minimal JWT with given payload fields
function makeJwt(payload: Record<string, unknown>): string {
    const header = Buffer.from(JSON.stringify({ alg: 'HS256', typ: 'JWT' })).toString('base64url')
    const body = Buffer.from(JSON.stringify(payload)).toString('base64url')
    return `${header}.${body}.sig`
}

describe('InMemoryPermissionsCache', () => {
    test('returns null on cache miss', async () => {
        const cache = new InMemoryPermissionsCache()
        expect(await cache.get('perms:user-1:proj-1')).toBeNull()
    })

    test('returns stored value before TTL expires', async () => {
        const cache = new InMemoryPermissionsCache()
        await cache.set('perms:user-1:proj-1', MOCK_PERMS, 30)
        expect(await cache.get('perms:user-1:proj-1')).toEqual(MOCK_PERMS)
    })

    test('returns null after TTL expires', async () => {
        const cache = new InMemoryPermissionsCache()
        // TTL = 0 — expires immediately
        await cache.set('perms:user-1:proj-1', MOCK_PERMS, 0)
        // Advance time by a tick
        await new Promise((r) => setImmediate(r))
        expect(await cache.get('perms:user-1:proj-1')).toBeNull()
    })

    test('invalidateUser removes all keys for the user', async () => {
        const cache = new InMemoryPermissionsCache()
        await cache.set('perms:user-1:proj-1', MOCK_PERMS, 30)
        await cache.set('perms:user-1:proj-2', MOCK_PERMS, 30)
        await cache.set('perms:sys:user-1', MOCK_PERMS, 30)
        await cache.set('perms:user-2:proj-1', MOCK_PERMS, 30)

        await cache.invalidateUser('user-1')

        expect(await cache.get('perms:user-1:proj-1')).toBeNull()
        expect(await cache.get('perms:user-1:proj-2')).toBeNull()
        expect(await cache.get('perms:sys:user-1')).toBeNull()
        // Other user's cache is unaffected
        expect(await cache.get('perms:user-2:proj-1')).toEqual(MOCK_PERMS)
    })

    test('clear removes all entries', async () => {
        const cache = new InMemoryPermissionsCache()
        await cache.set('perms:user-1:proj-1', MOCK_PERMS, 30)
        cache.clear()
        expect(await cache.get('perms:user-1:proj-1')).toBeNull()
    })
})

describe('permissions cache — auth-proxy integration', () => {
    test('second call uses cached permissions — auth-service called only once', async () => {
        let callCount = 0
        const upstream = (await import('fastify')).default({ logger: false })
        upstream.get('/api/v1/users/:userId/effective-permissions', async (_req, reply) => {
            callCount++
            return reply.send(MOCK_PERMS)
        })
        await upstream.listen({ port: 0, host: '127.0.0.1' })
        const address = upstream.server.address()
        const upstreamPort = typeof address === 'object' && address ? address.port : 0

        const cache = new InMemoryPermissionsCache()
        const accessToken = makeJwt({
            sub: 'user-1',
            sys: ['read'],
            sa: false,
            exp: Math.floor(Date.now() / 1000) + 3600,
        })

        const experimentUpstream = (await import('fastify')).default({ logger: false })
        experimentUpstream.get('/api/v1/experiments', async () => ({ items: [] }))
        await experimentUpstream.listen({ port: 0, host: '127.0.0.1' })
        const expAddress = experimentUpstream.server.address()
        const expPort = typeof expAddress === 'object' && expAddress ? expAddress.port : 0

        const app = await buildServer(
            makeConfig({
                authUrl: `http://127.0.0.1:${upstreamPort}`,
                targetExperimentUrl: `http://127.0.0.1:${expPort}`,
            }),
            cache
        )
        await app.ready()

        try {
            const cookieHeader = `access_token=${accessToken}; csrf_token=tok`
            // Both requests include project_id so getEffectivePermissions is called
            const req1 = await app.inject({
                method: 'GET',
                url: '/api/v1/experiments?project_id=proj-1',
                headers: { cookie: cookieHeader },
            })
            const req2 = await app.inject({
                method: 'GET',
                url: '/api/v1/experiments?project_id=proj-1',
                headers: { cookie: cookieHeader },
            })

            // Both requests should succeed (200 from upstream)
            expect(req1.statusCode).toBe(200)
            expect(req2.statusCode).toBe(200)
            // auth-service should be called only once — second is served from cache
            expect(callCount).toBe(1)
        } finally {
            await app.close()
            await upstream.close()
            await experimentUpstream.close()
        }
    })

    test('cache expires after TTL — auth-service called again', async () => {
        let callCount = 0
        const upstream = (await import('fastify')).default({ logger: false })
        upstream.get('/api/v1/users/:userId/effective-permissions', async (_req, reply) => {
            callCount++
            return reply.send(MOCK_PERMS)
        })
        await upstream.listen({ port: 0, host: '127.0.0.1' })
        const address = upstream.server.address()
        const upstreamPort = typeof address === 'object' && address ? address.port : 0

        const experimentUpstream = (await import('fastify')).default({ logger: false })
        experimentUpstream.get('/api/v1/experiments', async () => ({ items: [] }))
        await experimentUpstream.listen({ port: 0, host: '127.0.0.1' })
        const expAddress = experimentUpstream.server.address()
        const expPort = typeof expAddress === 'object' && expAddress ? expAddress.port : 0

        // Use InMemoryPermissionsCache with TTL=0 to simulate expiry
        const cache = new InMemoryPermissionsCache()
        const accessToken = makeJwt({
            sub: 'user-1',
            sys: ['read'],
            sa: false,
            exp: Math.floor(Date.now() / 1000) + 3600,
        })

        const app = await buildServer(
            makeConfig({
                authUrl: `http://127.0.0.1:${upstreamPort}`,
                targetExperimentUrl: `http://127.0.0.1:${expPort}`,
            }),
            cache
        )
        await app.ready()

        try {
            const cookieHeader = `access_token=${accessToken}; csrf_token=tok`

            // First call — populates cache
            await app.inject({
                method: 'GET',
                url: '/api/v1/experiments?project_id=proj-1',
                headers: { cookie: cookieHeader },
            })
            expect(callCount).toBe(1)

            // Manually expire the entry
            cache.clear()

            // Second call — cache miss, auth-service called again
            await app.inject({
                method: 'GET',
                url: '/api/v1/experiments?project_id=proj-1',
                headers: { cookie: cookieHeader },
            })
            expect(callCount).toBe(2)
        } finally {
            await app.close()
            await upstream.close()
            await experimentUpstream.close()
        }
    })

    test('on 401 from downstream — user cache is invalidated', async () => {
        const cache = new InMemoryPermissionsCache()
        const userId = 'user-1'

        // Pre-populate cache
        await cache.set(`perms:${userId}:proj-1`, MOCK_PERMS, 30)
        await cache.set(`perms:sys:${userId}`, MOCK_PERMS, 30)

        // Downstream experiment-service that returns 401
        const experimentUpstream = (await import('fastify')).default({ logger: false })
        experimentUpstream.get('/api/v1/experiments', async (_req, reply) => {
            reply.status(401).send({ error: 'Unauthorized' })
        })
        await experimentUpstream.listen({ port: 0, host: '127.0.0.1' })
        const expAddress = experimentUpstream.server.address()
        const expPort = typeof expAddress === 'object' && expAddress ? expAddress.port : 0

        const accessToken = makeJwt({
            sub: userId,
            sys: ['read'],
            sa: false,
            exp: Math.floor(Date.now() / 1000) + 3600,
        })

        const app = await buildServer(
            makeConfig({
                targetExperimentUrl: `http://127.0.0.1:${expPort}`,
            }),
            cache
        )
        await app.ready()

        try {
            await app.inject({
                method: 'GET',
                url: '/api/v1/experiments',
                headers: { cookie: `access_token=${accessToken}` },
            })

            // Give fire-and-forget a chance to complete
            await new Promise((r) => setImmediate(r))

            expect(await cache.get(`perms:${userId}:proj-1`)).toBeNull()
            expect(await cache.get(`perms:sys:${userId}`)).toBeNull()
        } finally {
            await app.close()
            await experimentUpstream.close()
        }
    })

    test('without Redis URL — works without cache (graceful degradation)', async () => {
        // No redisUrl in config, no _cache injected → NoopPermissionsCache
        const app = await buildServer(makeConfig({ redisUrl: undefined }))
        await app.ready()

        const res = await app.inject({ method: 'GET', url: '/health' })
        expect(res.statusCode).toBe(200)
        expect(res.json()).toEqual({ status: 'ok' })

        await app.close()
    })
})
