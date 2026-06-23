import fastify from 'fastify'
import cookie from '@fastify/cookie'
import { parseCookiesLocal, registerAuthProxy, RegisterAuthProxyOptions } from '../src/proxyFactory'

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

async function makeUpstream(handler: (headers: Record<string, string>) => void) {
    const up = fastify({ logger: false })
    up.all('/*', async (req, reply) => {
        handler(req.headers as Record<string, string>)
        reply.status(200).send({ ok: true })
    })
    await up.listen({ port: 0, host: '127.0.0.1' })
    const addr = up.server.address()
    const port = typeof addr === 'object' && addr ? addr.port : 0
    return { up, upstream: `http://127.0.0.1:${port}` }
}

async function makeProxy(upstream: string, opts: Omit<RegisterAuthProxyOptions, 'upstream'>) {
    const app = fastify({ logger: false })
    await app.register(cookie)
    await registerAuthProxy(app, { upstream, ...opts })
    await app.ready()
    return app
}

// ---------------------------------------------------------------------------
// parseCookiesLocal
// ---------------------------------------------------------------------------

describe('parseCookiesLocal', () => {
    it('parses a simple key=value pair', () => {
        expect(parseCookiesLocal('a=1')).toEqual({ a: '1' })
    })

    it('parses multiple pairs separated by semicolons', () => {
        expect(parseCookiesLocal('a=1; b=2; c=3')).toEqual({ a: '1', b: '2', c: '3' })
    })

    it('decodes percent-encoded values', () => {
        expect(parseCookiesLocal('token=hello%20world')).toEqual({ token: 'hello world' })
    })

    it('returns empty object for undefined input', () => {
        expect(parseCookiesLocal(undefined)).toEqual({})
    })

    it('returns empty object for empty string', () => {
        expect(parseCookiesLocal('')).toEqual({})
    })

    it('handles a cookie with an embedded equals sign in its value', () => {
        const result = parseCookiesLocal('jwt=header.payload.sig==')
        expect(result['jwt']).toBe('header.payload.sig==')
    })

    it('ignores malformed pairs without an equals sign', () => {
        expect(parseCookiesLocal('bad; a=1')).toEqual({ a: '1' })
    })
})

// ---------------------------------------------------------------------------
// registerAuthProxy — header injection
// ---------------------------------------------------------------------------

describe('registerAuthProxy — header injection', () => {
    let capturedHeaders: Record<string, string> = {}
    let up: ReturnType<typeof fastify>
    let upstreamUrl: string
    let proxy: ReturnType<typeof fastify>

    beforeEach(() => { capturedHeaders = {} })

    afterEach(async () => {
        await proxy?.close()
        await up?.close()
    })

    it('injects Authorization from the access_token cookie', async () => {
        ;({ up, upstream: upstreamUrl } = await makeUpstream((h) => { capturedHeaders = h }))
        proxy = await makeProxy(upstreamUrl, { prefix: '/api' })

        await proxy.inject({
            method: 'GET',
            url: '/api/hello',
            headers: { cookie: 'access_token=mytoken123' },
        })

        expect(capturedHeaders['authorization']).toBe('Bearer mytoken123')
    })

    it('omits Authorization when the cookie is absent', async () => {
        ;({ up, upstream: upstreamUrl } = await makeUpstream((h) => { capturedHeaders = h }))
        proxy = await makeProxy(upstreamUrl, { prefix: '/api' })

        await proxy.inject({ method: 'GET', url: '/api/hello' })

        expect(capturedHeaders['authorization']).toBeUndefined()
    })

    it('always sets X-Trace-Id and X-Request-Id', async () => {
        ;({ up, upstream: upstreamUrl } = await makeUpstream((h) => { capturedHeaders = h }))
        proxy = await makeProxy(upstreamUrl, { prefix: '/api' })

        await proxy.inject({ method: 'GET', url: '/api/hello' })

        expect(typeof capturedHeaders['x-trace-id']).toBe('string')
        expect(capturedHeaders['x-trace-id'].length).toBeGreaterThan(0)
        expect(typeof capturedHeaders['x-request-id']).toBe('string')
        expect(capturedHeaders['x-request-id'].length).toBeGreaterThan(0)
    })

    it('preserves x-trace-id sent by the client (normalised to no dashes)', async () => {
        ;({ up, upstream: upstreamUrl } = await makeUpstream((h) => { capturedHeaders = h }))
        proxy = await makeProxy(upstreamUrl, { prefix: '/api' })

        await proxy.inject({
            method: 'GET',
            url: '/api/hello',
            headers: { 'x-trace-id': 'aaaa-bbbb-cccc' },
        })

        expect(capturedHeaders['x-trace-id']).toBe('aaaabbbbcccc')
    })

    it('strips the cookie header by default (deleteCookie: true)', async () => {
        ;({ up, upstream: upstreamUrl } = await makeUpstream((h) => { capturedHeaders = h }))
        proxy = await makeProxy(upstreamUrl, { prefix: '/api' })

        await proxy.inject({
            method: 'GET',
            url: '/api/hello',
            headers: { cookie: 'access_token=tok; other=val' },
        })

        expect(capturedHeaders['cookie']).toBeUndefined()
    })

    it('keeps cookie header when deleteCookie is false', async () => {
        ;({ up, upstream: upstreamUrl } = await makeUpstream((h) => { capturedHeaders = h }))
        proxy = await makeProxy(upstreamUrl, { prefix: '/api', deleteCookie: false })

        await proxy.inject({
            method: 'GET',
            url: '/api/hello',
            headers: { cookie: 'access_token=tok; other=val' },
        })

        expect(capturedHeaders['cookie']).toBeDefined()
    })

    it('injects content-type for POST when missing and ensureJsonContentType is true (default)', async () => {
        ;({ up, upstream: upstreamUrl } = await makeUpstream((h) => { capturedHeaders = h }))
        proxy = await makeProxy(upstreamUrl, { prefix: '/api' })

        await proxy.inject({ method: 'POST', url: '/api/resource', payload: '{}' })

        expect(capturedHeaders['content-type']).toBe('application/json')
    })

    it('does not override an existing content-type', async () => {
        ;({ up, upstream: upstreamUrl } = await makeUpstream((h) => { capturedHeaders = h }))
        proxy = await makeProxy(upstreamUrl, { prefix: '/api' })

        await proxy.inject({
            method: 'POST',
            url: '/api/resource',
            headers: { 'content-type': 'text/plain' },
            payload: 'hello',
        })

        expect(capturedHeaders['content-type']).toMatch(/^text\/plain/)
    })

    it('does not inject content-type when ensureJsonContentType is false', async () => {
        ;({ up, upstream: upstreamUrl } = await makeUpstream((h) => { capturedHeaders = h }))
        proxy = await makeProxy(upstreamUrl, { prefix: '/api', ensureJsonContentType: false })

        await proxy.inject({ method: 'POST', url: '/api/resource', payload: '{}' })

        expect(capturedHeaders['content-type']).toBeUndefined()
    })

    it('uses a custom cookie name when accessCookieName is provided', async () => {
        ;({ up, upstream: upstreamUrl } = await makeUpstream((h) => { capturedHeaders = h }))
        proxy = await makeProxy(upstreamUrl, {
            prefix: '/api',
            accessCookieName: 'my_token',
        })

        await proxy.inject({
            method: 'GET',
            url: '/api/hello',
            headers: { cookie: 'my_token=custom_value' },
        })

        expect(capturedHeaders['authorization']).toBe('Bearer custom_value')
    })
})
