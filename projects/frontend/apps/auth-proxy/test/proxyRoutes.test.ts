import fastify from 'fastify'
import type { Config } from '../src/config'
import { buildServer } from '../src/index'

function makeJwt(payload: Record<string, unknown>): string {
    return `header.${Buffer.from(JSON.stringify(payload)).toString('base64url')}.signature`
}

function makeConfig(overrides: Partial<Config>): Config {
    return {
        port: 0,
        targetExperimentUrl: 'http://example.invalid',
        targetTelemetryUrl: 'http://example.invalid',
        targetConfigServiceUrl: 'http://example.invalid',
        targetScriptUrl: 'http://example.invalid',
        authUrl: 'http://example.invalid',
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
        ...overrides,
    }
}

async function listen(upstream: ReturnType<typeof fastify>): Promise<string> {
    await upstream.listen({ port: 0, host: '127.0.0.1' })
    const address = upstream.server.address()
    const port = typeof address === 'object' && address ? address.port : 0
    return `http://127.0.0.1:${port}`
}

describe('declarative proxy registration', () => {
    test('keeps internal config bulk blocked ahead of both config and experiment proxies', async () => {
        const configUpstream = fastify({ logger: false })
        const experimentUpstream = fastify({ logger: false })
        let configCalls = 0
        let experimentCalls = 0
        configUpstream.all('/*', async () => {
            configCalls += 1
            return { source: 'config' }
        })
        experimentUpstream.all('/*', async () => {
            experimentCalls += 1
            return { source: 'experiment' }
        })
        const configUrl = await listen(configUpstream)
        const experimentUrl = await listen(experimentUpstream)
        const app = await buildServer(makeConfig({
            targetConfigServiceUrl: configUrl,
            targetExperimentUrl: experimentUrl,
        }))

        try {
            const response = await app.inject({
                method: 'GET',
                url: '/api/config-service/v1/configs/bulk',
            })
            expect(response.statusCode).toBe(404)
            expect(response.json()).toEqual({ error: 'Not Found' })
            expect(configCalls).toBe(0)
            expect(experimentCalls).toBe(0)
        } finally {
            await app.close()
            await configUpstream.close()
            await experimentUpstream.close()
        }
    })

    test('routes config and script prefixes with trusted identity headers', async () => {
        const configUpstream = fastify({ logger: false })
        const scriptUpstream = fastify({ logger: false })
        const seen: Array<{ service: string; url: string; headers: Record<string, unknown> }> = []
        configUpstream.all('/*', async (request) => {
            seen.push({ service: 'config', url: request.url, headers: request.headers })
            return { ok: true }
        })
        scriptUpstream.all('/*', async (request) => {
            seen.push({ service: 'script', url: request.url, headers: request.headers })
            return { ok: true }
        })
        const configUrl = await listen(configUpstream)
        const scriptUrl = await listen(scriptUpstream)
        const app = await buildServer(makeConfig({
            targetConfigServiceUrl: configUrl,
            targetScriptUrl: scriptUrl,
        }))
        const access = makeJwt({ sub: 'trusted-user', sa: true, sys: [] })
        const headers = {
            cookie: `access_token=${access}`,
            'x-user-id': 'spoofed-user',
            'x-user-is-superadmin': 'false',
            'x-user-permissions': 'spoofed.permission',
        }

        try {
            expect((await app.inject({
                method: 'GET',
                url: '/api/config-service/v1/configs/example',
                headers,
            })).statusCode).toBe(200)
            expect((await app.inject({
                method: 'GET',
                url: '/api/v1/scripts',
                headers: { cookie: `access_token=${access}` },
            })).statusCode).toBe(200)

            expect(seen).toHaveLength(2)
            expect(seen[0].service).toBe('config')
            expect(seen[0].url).toBe('/api/v1/configs/example')
            expect(seen[0].headers['x-user-id']).toBe('trusted-user')
            expect(seen[0].headers['x-user-is-superadmin']).toBe('true')
            expect(seen[0].headers['x-user-permissions']).toBe('')
            expect(seen[0].headers.cookie).toBeUndefined()

            expect(seen[1].service).toBe('script')
            expect(seen[1].headers['x-user-id']).toBe('trusted-user')
            expect(seen[1].headers['x-user-is-superadmin']).toBe('true')
            expect(seen[1].headers.cookie).toBeUndefined()
        } finally {
            await app.close()
            await configUpstream.close()
            await scriptUpstream.close()
        }
    })
})
