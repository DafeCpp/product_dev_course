import fastify, { type FastifyInstance, type FastifyReply, type FastifyRequest } from 'fastify'
import { buildServer, type Config, type PermissionsCache } from '../src/index'

export const CSRF = 'csrf-token'

export function makeConfig(authUrl: string, overrides: Partial<Config> = {}): Config {
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
        ...overrides,
    }
}

export type UpstreamHandler = (request: FastifyRequest, reply: FastifyReply) => unknown

export interface Upstream {
    url: string
    calls: Array<{ method: string; url: string; headers: Record<string, unknown>; body: unknown }>
    close: () => Promise<void>
}

/** Starts a stub upstream; every request is routed to `routes["METHOD /path"]` (path without query). */
export async function startUpstream(routes: Record<string, UpstreamHandler>): Promise<Upstream> {
    const server = fastify({ logger: false })
    const calls: Upstream['calls'] = []
    server.all('/*', async (request, reply) => {
        calls.push({
            method: request.method,
            url: request.url,
            headers: request.headers,
            body: request.body,
        })
        const path = request.url.split('?')[0]
        const handler = routes[`${request.method} ${path}`]
        if (!handler) return reply.status(404).send({ error: 'no stub' })
        return handler(request, reply)
    })
    await server.listen({ port: 0, host: '127.0.0.1' })
    const address = server.server.address()
    const port = typeof address === 'object' && address ? address.port : 0
    return { url: `http://127.0.0.1:${port}`, calls, close: () => server.close() }
}

export interface Proxy {
    app: FastifyInstance
    upstream: Upstream
    dispose: () => Promise<void>
}

export async function startProxy(
    routes: Record<string, UpstreamHandler>,
    overrides: Partial<Config> = {},
    cache?: PermissionsCache
): Promise<Proxy> {
    const upstream = await startUpstream(routes)
    const app = await buildServer(
        makeConfig(upstream.url, {
            targetExperimentUrl: upstream.url,
            targetTelemetryUrl: upstream.url,
            targetConfigServiceUrl: upstream.url,
            targetScriptUrl: upstream.url,
            ...overrides,
        }),
        cache
    )
    await app.ready()
    return {
        app,
        upstream,
        dispose: async () => {
            await app.close()
            await upstream.close()
        },
    }
}

export function setCookies(res: { headers: { 'set-cookie'?: unknown } }): string[] {
    const value = res.headers['set-cookie']
    if (!value) return []
    return (Array.isArray(value) ? value : [value]).filter(
        (item): item is string => typeof item === 'string'
    )
}

export function cookieNamed(res: { headers: { 'set-cookie'?: unknown } }, name: string) {
    return setCookies(res).find((item) => item.startsWith(`${name}=`))
}

/** Headers of a browser session that passes the CSRF checks; an `undefined` extra removes the header. */
export function sessionHeaders(cookie: string, extra: Record<string, string | undefined> = {}) {
    const headers: Record<string, string | undefined> = {
        origin: 'http://localhost:3000',
        cookie: `${cookie}; csrf_token=${CSRF}`,
        'x-csrf-token': CSRF,
        'content-type': 'application/json',
        ...extra,
    }
    return Object.fromEntries(
        Object.entries(headers).filter((entry): entry is [string, string] => entry[1] !== undefined)
    )
}

export function makeJwt(payload: Record<string, unknown>): string {
    const encode = (value: unknown) => Buffer.from(JSON.stringify(value)).toString('base64url')
    return `${encode({ alg: 'HS256', typ: 'JWT' })}.${encode(payload)}.sig`
}
