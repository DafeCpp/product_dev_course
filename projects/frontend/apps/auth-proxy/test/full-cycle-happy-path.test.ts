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

describe('full cycle happy path (auth-proxy)', () => {
    test('project -> experiment -> run -> sensors -> capture session -> telemetry -> finish', async () => {
        const userId = 'user-123'
        const projectId = 'project-1'
        const experimentId = 'exp-1'
        const runId = 'run-1'
        const sensorId = 'sensor-1'
        const captureSessionId = 'cs-1'

        // ---------- mock auth-service ----------
        const authUpstream = fastify({ logger: false })
        const accessToken = makeJwt({ sub: userId, sa: false, sys: [] })
        const refreshToken = 'refresh-123'

        // Login/refresh
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

        // Projects CRUD (simplified)
        const authSeen: any[] = []
        authUpstream.post('/projects', async (req) => {
            authSeen.push({ path: '/projects', headers: req.headers, body: req.body })
            return { id: projectId, name: (req.body as any)?.name || 'P', owner_id: userId }
        })
        // Effective permissions lookup used by auth-proxy to inject RBAC headers
        authUpstream.get('/api/v1/users/:userId/effective-permissions', async (req) => {
            const query = req.query as Record<string, string>
            return {
                user_id: userId,
                is_superadmin: false,
                system_permissions: [],
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
            expSeen.push({ path: '/api/v1/experiments', headers: req.headers, body: req.body })
            return { id: experimentId }
        })
        expUpstream.post(`/api/v1/experiments/${experimentId}/runs`, async (req) => {
            expSeen.push({ path: `/api/v1/experiments/${experimentId}/runs`, headers: req.headers, body: req.body })
            return { id: runId }
        })
        expUpstream.post('/api/v1/sensors', async (req) => {
            expSeen.push({ path: '/api/v1/sensors', headers: req.headers, body: req.body })
            return { id: sensorId, project_id: (req.body as any)?.project_id || projectId, name: 'S' }
        })
        expUpstream.post(`/api/v1/runs/${runId}/capture-sessions`, async (req) => {
            expSeen.push({ path: `/api/v1/runs/${runId}/capture-sessions`, headers: req.headers, body: req.body })
            return { id: captureSessionId, run_id: runId, project_id: projectId, status: 'running', ordinal_number: 1 }
        })
        expUpstream.post(`/api/v1/runs/${runId}/capture-sessions/${captureSessionId}/stop`, async (req) => {
            expSeen.push({ path: `/api/v1/runs/${runId}/capture-sessions/${captureSessionId}/stop`, headers: req.headers, body: req.body })
            return { id: captureSessionId, status: 'succeeded' }
        })
        expUpstream.patch(`/api/v1/runs/${runId}`, async (req) => {
            expSeen.push({ path: `/api/v1/runs/${runId}`, headers: req.headers, body: req.body })
            return { id: runId, status: (req.body as any)?.status || 'running' }
        })

        await expUpstream.listen({ port: 0, host: '127.0.0.1' })
        const expAddr = expUpstream.server.address()
        const expPort = typeof expAddr === 'object' && expAddr ? expAddr.port : 0

        // ---------- mock telemetry-ingest-service ----------
        const telUpstream = fastify({ logger: false })
        const telSeen: any[] = []
        telUpstream.post('/api/v1/telemetry', async (req) => {
            telSeen.push({ path: '/api/v1/telemetry', headers: req.headers, body: req.body })
            return { status: 'accepted', accepted: 1 }
        })
        telUpstream.get('/api/v1/telemetry/stream', async (req, reply) => {
            telSeen.push({ path: '/api/v1/telemetry/stream', headers: req.headers, query: req.query })
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
            const access = setCookies.map((c) => cookieValue(c, 'access_token')).find(Boolean)
            const csrf = setCookies.map((c) => cookieValue(c, 'csrf_token')).find(Boolean)
            expect(access).toBeDefined()
            expect(csrf).toBeDefined()
            const cookieHeader = `access_token=${access}; csrf_token=${csrf}`

            // 2) create project via /projects (requires CSRF)
            const createProjectBlocked = await app.inject({
                method: 'POST',
                url: '/projects',
                headers: { origin: 'http://localhost:3000', cookie: cookieHeader, 'content-type': 'application/json' },
                payload: JSON.stringify({ name: 'P1' }),
            })
            expect(createProjectBlocked.statusCode).toBe(403)

            const createProjectOk = await app.inject({
                method: 'POST',
                url: '/projects',
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: cookieHeader,
                    'x-csrf-token': String(csrf),
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ name: 'P1' }),
            })
            expect(createProjectOk.statusCode).toBe(200)
            expect(createProjectOk.json().id).toBe(projectId)

            // Ensure /projects proxy injected Authorization from cookie access token
            expect(authSeen.length).toBe(1)
            expect(authSeen[0].headers.authorization).toBe(`Bearer ${access}`)

            // 3) create experiment (requires CSRF + should set X-Project-* headers using membership)
            const expRes = await app.inject({
                method: 'POST',
                url: `/api/v1/experiments?project_id=${projectId}`,
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: cookieHeader,
                    'x-csrf-token': String(csrf),
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ name: 'E1' }),
            })
            expect(expRes.statusCode).toBe(200)

            const expCall = expSeen.find((c) => c.path === '/api/v1/experiments')
            expect(expCall).toBeDefined()
            expect(expCall.headers.authorization).toBe(`Bearer ${access}`)
            expect(expCall.headers['x-user-id']).toBe(userId)
            expect(expCall.headers['x-project-id']).toBe(projectId)
            // RBAC headers injected by auth-proxy from effective-permissions endpoint
            expect(expCall.headers['x-user-is-superadmin']).toBe('false')
            expect(expCall.headers['x-user-permissions']).toBe('experiments.create,experiments.view')
            // Old X-Project-Role header must NOT be forwarded
            expect(expCall.headers['x-project-role']).toBeUndefined()

            // 4) create run
            const runRes = await app.inject({
                method: 'POST',
                url: `/api/v1/experiments/${experimentId}/runs?project_id=${projectId}`,
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: cookieHeader,
                    'x-csrf-token': String(csrf),
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ name: 'R1' }),
            })
            expect(runRes.statusCode).toBe(200)
            expect(runRes.json().id).toBe(runId)

            // 5) create sensor(s)
            const sensorRes = await app.inject({
                method: 'POST',
                url: `/api/v1/sensors?project_id=${projectId}`,
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: cookieHeader,
                    'x-csrf-token': String(csrf),
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ project_id: projectId, name: 'S1', type: 't', input_unit: 'u', display_unit: 'u' }),
            })
            expect(sensorRes.statusCode).toBe(200)
            expect(sensorRes.json().id).toBe(sensorId)

            // 6) start run (status running)
            const startRun = await app.inject({
                method: 'PATCH',
                url: `/api/v1/runs/${runId}?project_id=${projectId}`,
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: cookieHeader,
                    'x-csrf-token': String(csrf),
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ status: 'running' }),
            })
            expect(startRun.statusCode).toBe(200)

            // 7) create capture session
            const csRes = await app.inject({
                method: 'POST',
                url: `/api/v1/runs/${runId}/capture-sessions?project_id=${projectId}`,
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: cookieHeader,
                    'x-csrf-token': String(csrf),
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ ordinal_number: 1, status: 'running' }),
            })
            expect(csRes.statusCode).toBe(200)
            expect(csRes.json().id).toBe(captureSessionId)

            // 8) telemetry ingest/stream should NOT require CSRF, even if session cookies are present
            const tIngest = await app.inject({
                method: 'POST',
                url: '/api/v1/telemetry',
                headers: {
                    cookie: cookieHeader, // present, but endpoint is excluded from CSRF
                    authorization: 'Bearer sensor-token-1',
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({
                    sensor_id: sensorId,
                    run_id: runId,
                    capture_session_id: captureSessionId,
                    readings: [{ timestamp: '2026-01-01T00:00:00Z', raw_value: 1, meta: {} }],
                }),
            })
            expect(tIngest.statusCode).toBe(200)

            const tStream = await app.inject({
                method: 'GET',
                url: `/api/v1/telemetry/stream?sensor_id=${sensorId}`,
                headers: {
                    cookie: cookieHeader,
                    authorization: 'Bearer sensor-token-1',
                },
            })
            expect(tStream.statusCode).toBe(200)

            expect(telSeen.length).toBeGreaterThan(0)
            expect(telSeen[0].headers.authorization).toBe('Bearer sensor-token-1')

            // 9) stop capture session and finish run
            const stopCs = await app.inject({
                method: 'POST',
                url: `/api/v1/runs/${runId}/capture-sessions/${captureSessionId}/stop?project_id=${projectId}`,
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: cookieHeader,
                    'x-csrf-token': String(csrf),
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ status: 'succeeded' }),
            })
            expect(stopCs.statusCode).toBe(200)

            const finishRun = await app.inject({
                method: 'PATCH',
                url: `/api/v1/runs/${runId}?project_id=${projectId}`,
                headers: {
                    origin: 'http://localhost:3000',
                    cookie: cookieHeader,
                    'x-csrf-token': String(csrf),
                    'content-type': 'application/json',
                },
                payload: JSON.stringify({ status: 'succeeded' }),
            })
            expect(finishRun.statusCode).toBe(200)
        } finally {
            await app.close()
            await authUpstream.close()
            await expUpstream.close()
            await telUpstream.close()
        }
    })
})

