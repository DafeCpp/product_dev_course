import { randomUUID } from 'crypto'
import type { FastifyReply, FastifyRequest } from 'fastify'
import type { Config } from './config'

export type AuthTokens = {
    access_token: string
    refresh_token?: string
    expires_in?: number
    refresh_expires_in?: number
    token_type?: string
    [key: string]: unknown
}

export function parseCookies(header: string | undefined): Record<string, string> {
    if (!header) return {}
    return header
        .split(';')
        .map((v) => v.trim())
        .filter(Boolean)
        .reduce<Record<string, string>>((acc, pair) => {
            const idx = pair.indexOf('=')
            if (idx === -1) return acc
            const key = decodeURIComponent(pair.slice(0, idx).trim())
            const val = decodeURIComponent(pair.slice(idx + 1).trim())
            acc[key] = val
            return acc
        }, {})
}

export function setAuthCookies(
    reply: FastifyReply,
    cfg: Config,
    tokens: AuthTokens,
    opts?: { skipRefresh?: boolean }
) {
    const accessTtl = tokens.expires_in ?? cfg.accessTtlSec
    reply.setCookie(cfg.accessCookieName, tokens.access_token, {
        httpOnly: true,
        secure: cfg.cookieSecure,
        sameSite: cfg.cookieSameSite,
        domain: cfg.cookieDomain,
        path: '/',
        maxAge: accessTtl,
    })

    if (!opts?.skipRefresh && tokens.refresh_token) {
        const refreshTtl = tokens.refresh_expires_in ?? cfg.refreshTtlSec
        reply.setCookie(cfg.refreshCookieName, tokens.refresh_token, {
            httpOnly: true,
            secure: cfg.cookieSecure,
            sameSite: cfg.cookieSameSite,
            domain: cfg.cookieDomain,
            path: '/',
            maxAge: refreshTtl,
        })
    }
}

export function clearAuthCookies(reply: FastifyReply, cfg: Config) {
    reply.clearCookie(cfg.accessCookieName, { path: '/' })
    reply.clearCookie(cfg.refreshCookieName, { path: '/' })
    reply.clearCookie('csrf_token', { path: '/' })
}

export function setCsrfCookie(reply: FastifyReply, cfg: Config) {
    // Double-submit cookie: client must echo cookie value in X-CSRF-Token header.
    // Cookie must NOT be HttpOnly (frontend reads it).
    reply.setCookie('csrf_token', generateUUID(), {
        httpOnly: false,
        secure: cfg.cookieSecure,
        sameSite: cfg.cookieSameSite,
        domain: cfg.cookieDomain,
        path: '/',
        maxAge: cfg.refreshTtlSec,
    })
}

export function generateUUID(): string {
    return randomUUID().replace(/-/g, '')
}

export function normalizeUUID(uuid: string | undefined): string | undefined {
    return uuid ? uuid.replace(/-/g, '') : undefined
}

export function getTraceContext(request: FastifyRequest): {
    traceId: string
    requestId: string
} {
    const traceId = normalizeUUID(request.headers['x-trace-id'] as string) || generateUUID()
    const requestId = generateUUID()
    return { traceId, requestId }
}

export function getOutgoingRequestHeaders(traceId: string): {
    'X-Trace-Id': string
    'X-Request-Id': string
} {
    return {
        'X-Trace-Id': traceId,
        'X-Request-Id': generateUUID(),
    }
}

export function decodeJwtPayload(token: string): Record<string, unknown> | null {
    const parts = token.split('.')
    if (parts.length !== 3) return null
    try {
        const payload = parts[1].replace(/-/g, '+').replace(/_/g, '/')
        const padded = payload.padEnd(payload.length + ((4 - (payload.length % 4)) % 4), '=')
        const decoded = Buffer.from(padded, 'base64').toString('utf-8')
        const data = JSON.parse(decoded)
        return typeof data === 'object' && data ? (data as Record<string, unknown>) : null
    } catch {
        return null
    }
}

export function isJwtExpired(token: string, skewSec = 30): boolean {
    const payload = decodeJwtPayload(token)
    const expRaw = payload?.exp
    let exp: number | null = null
    if (typeof expRaw === 'number') {
        exp = expRaw
    } else if (typeof expRaw === 'string') {
        const parsed = Number(expRaw)
        exp = Number.isFinite(parsed) ? parsed : null
    }
    if (exp === null) return false
    // Some issuers may provide exp in milliseconds.
    if (exp > 1_000_000_000_000) exp = Math.floor(exp / 1000)
    const nowSec = Math.floor(Date.now() / 1000)
    return exp <= nowSec + skewSec
}

export function decodeJwtUserId(token: string): string | null {
    const payload = decodeJwtPayload(token)
    const userId = payload?.sub ?? payload?.user_id
    return typeof userId === 'string' ? userId : null
}
