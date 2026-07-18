import type { FastifyReply } from 'fastify'
import type { Config } from '../src/config'
import {
    clearAuthCookies,
    decodeJwtPayload,
    decodeJwtUserId,
    generateUUID,
    getOutgoingRequestHeaders,
    isJwtExpired,
    normalizeUUID,
    parseCookies,
    setAuthCookies,
    setCsrfCookie,
} from '../src/security'

function makeJwt(payload: Record<string, unknown>): string {
    const encodedPayload = Buffer.from(JSON.stringify(payload)).toString('base64url')
    return `header.${encodedPayload}.signature`
}

const config: Config = {
    port: 0,
    targetExperimentUrl: 'http://example.invalid',
    targetTelemetryUrl: 'http://example.invalid',
    targetConfigServiceUrl: 'http://example.invalid',
    targetScriptUrl: 'http://example.invalid',
    authUrl: 'http://example.invalid',
    corsOrigins: ['http://localhost:3000'],
    cookieSecure: true,
    cookieSameSite: 'strict',
    cookieDomain: 'example.com',
    accessCookieName: 'access_token',
    refreshCookieName: 'refresh_token',
    accessTtlSec: 900,
    refreshTtlSec: 1_209_600,
    rateLimitWindowMs: 60_000,
    rateLimitMax: 60,
    logLevel: 'silent',
}

function makeReply() {
    return {
        setCookie: jest.fn(),
        clearCookie: jest.fn(),
    } as unknown as FastifyReply
}

describe('security helpers', () => {
    it('parses cookie headers', () => {
        expect(parseCookies('a=1; b=hello%20world; c=%7B%22x%22%3A1%7D')).toEqual({
            a: '1',
            b: 'hello world',
            c: '{"x":1}',
        })
    })

    it('handles empty, malformed, and equals-containing cookie values', () => {
        expect(parseCookies(undefined)).toEqual({})
        expect(parseCookies('')).toEqual({})
        expect(parseCookies('bad; jwt=header.payload.sig==')).toEqual({
            jwt: 'header.payload.sig==',
        })
    })

    it('normalizes UUIDs by removing dashes', () => {
        expect(normalizeUUID('aaaa-bbbb-cccc')).toBe('aaaabbbbcccc')
        expect(normalizeUUID(undefined)).toBeUndefined()
    })

    it('generates UUIDs without dashes', () => {
        expect(generateUUID()).toMatch(/^[0-9a-f]{32}$/)
    })

    it('preserves trace id and generates request id for outgoing requests', () => {
        const headers = getOutgoingRequestHeaders('trace-123')

        expect(headers['X-Trace-Id']).toBe('trace-123')
        expect(headers['X-Request-Id']).toHaveLength(32)
    })

    it('decodes JWT payload and user id', () => {
        const token = makeJwt({ sub: 'user-1', sys: ['projects.read'] })

        expect(decodeJwtPayload(token)).toEqual({ sub: 'user-1', sys: ['projects.read'] })
        expect(decodeJwtUserId(token)).toBe('user-1')
    })

    it('rejects malformed JWT payloads', () => {
        expect(decodeJwtPayload('not-a-jwt')).toBeNull()
        expect(decodeJwtPayload('header.invalid.signature')).toBeNull()
        expect(decodeJwtUserId(makeJwt({ sub: 42 }))).toBeNull()
    })

    it('treats JWT as expired when exp is inside skew window', () => {
        const nowSec = Math.floor(Date.now() / 1000)
        const token = makeJwt({ exp: nowSec + 10 })

        expect(isJwtExpired(token, 30)).toBe(true)
    })

    it('does not expire JWTs without exp claim', () => {
        expect(isJwtExpired(makeJwt({ sub: 'user-1' }))).toBe(false)
    })

    it('sets access and refresh cookies with configured security options', () => {
        const reply = makeReply()

        setAuthCookies(reply, config, {
            access_token: 'access',
            refresh_token: 'refresh',
            expires_in: 60,
            refresh_expires_in: 120,
        })

        expect(reply.setCookie).toHaveBeenNthCalledWith(
            1,
            'access_token',
            'access',
            expect.objectContaining({
                httpOnly: true,
                secure: true,
                sameSite: 'strict',
                domain: 'example.com',
                path: '/',
                maxAge: 60,
            })
        )
        expect(reply.setCookie).toHaveBeenNthCalledWith(
            2,
            'refresh_token',
            'refresh',
            expect.objectContaining({ httpOnly: true, maxAge: 120 })
        )
    })

    it('sets a readable CSRF cookie and clears all session cookies', () => {
        const reply = makeReply()

        setCsrfCookie(reply, config)
        clearAuthCookies(reply, config)

        expect(reply.setCookie).toHaveBeenCalledWith(
            'csrf_token',
            expect.stringMatching(/^[0-9a-f]{32}$/),
            expect.objectContaining({
                httpOnly: false,
                secure: true,
                sameSite: 'strict',
                maxAge: config.refreshTtlSec,
            })
        )
        expect(reply.clearCookie).toHaveBeenCalledWith('access_token', { path: '/' })
        expect(reply.clearCookie).toHaveBeenCalledWith('refresh_token', { path: '/' })
        expect(reply.clearCookie).toHaveBeenCalledWith('csrf_token', { path: '/' })
    })
})
