import fastify from 'fastify'
import { buildServer } from '../src/index'

function b64url(obj: any): string {
    return Buffer.from(JSON.stringify(obj), 'utf8').toString('base64url')
}

function makeJwt(payload: any): string {
    // Minimal unsigned-like JWT (signature is ignored by auth-proxy, it only decodes payload)
    return `${b64url({ alg: 'none', typ: 'JWT' })}.${b64url(payload)}.sig`
}

function getSetCookies(res: any): string[] {
    const sc = res.headers['set-cookie']
    if (!sc) return []
    return Array.isArray(sc) ? sc : [String(sc)]
}

function cookieValue(setCookieHeader: string, name: string): string | undefined {
    const part = setCookieHeader.split(';')[0]
    const idx = part.indexOf('=')
    if (idx === -1) return undefined
    const k = part.slice(0, idx)
    const v = part.slice(idx + 1)
    return k === name ? v : undefined
}

describe('full cycle integration (auth-proxy)', () => {
    test('login -> CSRF -> state-changing /api -> telemetry (no CSRF)', async () => {
        // ---------- mock auth-service ----------
        const authUpstream = fastify({ logger: false })
        const accessToken = makeJwt({ sub: 'user-123', sa: false, sys: ['users.list', 'audit.read'] })
        const refreshToken = 'refresh-123'

        authUpstream.post('/auth/login', async () => ({
            access_token: accessToken,
            refresh_token: refreshToken,
            expires_in: 900,
            refresh_expires_in: 1209600,
            token_type: 'bearer',
        }))
        authUpstream.post('/auth/refresh', async () => ({
            access_token: accessToken,
            refresh_token: refreshToken,
            expires_in: 900,
            refresh_expires_in: 1209600,
            token_type: 'bearer',
        }))
        authUpstream.post('/auth/logout', async () => ({ ok: true }))
        authUpstream.get('/auth/me', async () => ({ id: 'user-123', username: 'u', email: 'u@e', is_active: true }))
        authUpstream.get('/api/v1/users/:userId/effective-permissions', async (req) => {
            const query = req.query as Record<string, string>
            return {
                user_id: 'user-123',
                is_superadmin: false,
                system_permissions: ['users.list', 'audit.read'],
                project_permissions: query.project_id ? ['experiments.create', 'experiments.view'] : [],
            }
        })

        await authUpstream.listen({ port: 0, host: '127.0.0.1' })
        const authAddr = authUpstream.server.address()
        const authPort = typeof authAddr === 'object' && authAddr ? authAddr.port : 0

        // ---------- mock experiment-service ----------
        const expUpstream = fastify({ logger: false })
        const expSeen: any[] = []
        expUpstream.post('/api/v1/experiments', async (req) => {
            expSeen.push({ headers: req.headers, body: req.body })
            return { id: 'exp-1' }
        })

        await expUpstream.listen({ port: 0, host: '127.0.0.1' })
        const expAddr = expUpstream.server.address()
        const expPort = typeof expAddr === 'object' && expAddr ? expAddr.port : 0

        // ---------- mock telemetry-ingest-service ----------
        const telUpstream = fastify({ logger: false })
        const telSeen: any[] = []
        telUpstream.post('/api/v1/telemetry', async (req) => {
            telSeen.push({ headers: req.headers, body: req.body })
            return { status: 'accepted', accepted: 1 }
        })
        telUpstream.get('/api/v1/telemetry/stream', async (req, reply) => {
            telSeen.push({ headers: req.headers, query: req.query })
            reply.header('Content-Type', 'text/event-stream')
            return 'event: telemetry\ndata: {"id":1}\n\n'
        })

        await telUpstream.listen({ port: 0, host: '127.0.0.1' })
        const telAddr = telUpstream.server.address()
        const telPort = typeof telAddr === 'object' && telAddr ? telAddr.port : 0

        // ---------- auth-proxy ----------
        const app = await buildServer({
            port: 0,
            targetExperimentUrl: `http://127.0.0.1:${expPort}`,
            targetTelemetryUrl: `http://127.0.0.1:${telPort}`,
            targetScriptUrl: 'http://example.invalid',
            authUrl: `http://127.0.0.1:${authPort}`,
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
            // 1) login -> cookies include csrf_token
            const loginRes = await app.inject({
                method: 'POST',
                url: '/auth/login',
                headers: { 'content-type': 'application/json' },
                payload: JSON.stringify({ username: 'u', password: 'p' }),
            })
            expect(loginRes.statusCode).toBe(200)
            const setCookies = getSetCookies(loginRes)
            expect(setCookies.length).toBeGreaterThan(0)

            const access = setCookies.map((c) => cookieValue(c, 'access_token')).find(Boolean)
            const csrf = setCookies.map((c) => cookieValue(c, 'csrf_token')).find(Boolean)
            expect(access).toBeDefined()
            expect(csrf).toBeDefined()

            const cookieHeader = `access_token=${access}; csrf_token=${csrf}`

            // 2) state-changing /api WITHOUT CSRF header -> blocked
            const blocked = await app.inject({
                method: 'POST',
                url: '/api/v1/experiments',
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: cookieHeader,
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ project_id: 'p1', name: 'e1' }),
            })
            expect(blocked.statusCode).toBe(403)

            // 3) state-changing /api WITH CSRF header -> allowed and forwarded
            const ok = await app.inject({
                method: 'POST',
                url: '/api/v1/experiments',
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: cookieHeader,
                    'x-csrf-token': String(csrf),
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ project_id: 'p1', name: 'e1' }),
            })
            expect(ok.statusCode).toBe(200)
            expect(ok.json()).toEqual({ id: 'exp-1' })

            expect(expSeen.length).toBe(1)
            const forwardedHeaders = expSeen[0].headers as Record<string, string>
            expect(forwardedHeaders.authorization).toBe(`Bearer ${access}`)
            // auth-proxy decodes JWT and forwards user id
            expect(forwardedHeaders['x-user-id']).toBe('user-123')
            // new RBAC permission headers
            expect(forwardedHeaders['x-user-is-superadmin']).toBe('false')
            expect(forwardedHeaders['x-user-system-permissions']).toBe('users.list,audit.read')
            expect(forwardedHeaders['x-user-permissions']).toBe('experiments.create,experiments.view')
            // X-Project-Role must NOT be forwarded
            expect(forwardedHeaders['x-project-role']).toBeUndefined()

            // 4) telemetry ingest should NOT require CSRF (uses Authorization sensor token)
            const tIngest = await app.inject({
                method: 'POST',
                url: '/api/v1/telemetry',
                headers: {
                    authorization: 'Bearer sensor-token-1',
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ sensor_id: 's1', readings: [{ timestamp: '2026-01-01T00:00:00Z', raw_value: 1, meta: {} }] }),
            })
            expect(tIngest.statusCode).toBe(200)
            expect(telSeen.length).toBeGreaterThan(0)
            expect((telSeen[0].headers as any).authorization).toBe('Bearer sensor-token-1')

            // 5) telemetry SSE stream should also be proxied with Authorization
            const tStream = await app.inject({
                method: 'GET',
                url: '/api/v1/telemetry/stream?sensor_id=s1',
                headers: {
                    authorization: 'Bearer sensor-token-1',
                },
            })
            expect(tStream.statusCode).toBe(200)
            expect(String(tStream.headers['content-type'])).toContain('text/event-stream')
            expect(tStream.body).toContain('event: telemetry')
        } finally {
            await app.close()
            await authUpstream.close()
            await expUpstream.close()
            await telUpstream.close()
        }
    })
})

