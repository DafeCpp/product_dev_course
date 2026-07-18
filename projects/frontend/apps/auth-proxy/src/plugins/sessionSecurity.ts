import type { FastifyReply, FastifyRequest } from 'fastify'
import fp from 'fastify-plugin'
import type { Config } from '../config'
import {
    type AuthTokens,
    clearAuthCookies,
    generateUUID,
    getOutgoingRequestHeaders,
    isJwtExpired,
    normalizeUUID,
    setAuthCookies,
    setCsrfCookie,
} from '../security'

interface SessionSecurityOptions {
    config: Config
}

function originFromReferer(referer: string | undefined): string | undefined {
    if (!referer) return undefined
    try {
        return new URL(referer).origin
    } catch {
        return undefined
    }
}

async function refreshAccessToken(
    request: FastifyRequest,
    reply: FastifyReply,
    config: Config
): Promise<string | null> {
    const refreshToken = request.cookies?.[config.refreshCookieName]
    if (!refreshToken) return null

    const traceId =
        request.traceId ||
        normalizeUUID(request.headers['x-trace-id'] as string | undefined) ||
        generateUUID()
    const response = await fetch(`${config.authUrl}/auth/refresh`, {
        method: 'POST',
        headers: {
            'content-type': 'application/json',
            ...getOutgoingRequestHeaders(traceId),
        },
        body: JSON.stringify({ refresh_token: refreshToken }),
    })

    if (!response.ok) {
        clearAuthCookies(reply, config)
        return null
    }

    const tokens = (await response.json()) as AuthTokens
    if (!tokens.access_token) return null
    setAuthCookies(reply, config, tokens)
    setCsrfCookie(reply, config)
    return tokens.access_token
}

export const sessionSecurityPlugin = fp<SessionSecurityOptions>(async (app, { config }) => {
    app.addHook('preHandler', async (request, reply) => {
        if (!['POST', 'PUT', 'PATCH', 'DELETE'].includes(request.method.toUpperCase())) return

        const url = request.url
        if (
            url === '/health' ||
            url.startsWith('/auth/login') ||
            url.startsWith('/auth/register') ||
            url.startsWith('/auth/refresh') ||
            url.startsWith('/auth/password-reset') ||
            url.startsWith('/api/v1/telemetry')
        ) return

        const hasSession = Boolean(
            request.cookies?.[config.accessCookieName] ||
            request.cookies?.[config.refreshCookieName]
        )
        if (!hasSession) return

        const origin =
            (request.headers.origin as string | undefined) ||
            originFromReferer(request.headers.referer as string | undefined)
        const allowed = origin && config.corsOrigins.some(
            (candidate) => candidate.toLowerCase() === origin.toLowerCase()
        )
        if (!allowed) {
            reply.status(403).send({ error: 'CSRF origin missing or invalid' })
            return
        }

        const header = request.headers['x-csrf-token']
        const csrfHeader = Array.isArray(header) ? header[0] : header
        if (!request.cookies?.csrf_token || request.cookies.csrf_token !== csrfHeader) {
            reply.status(403).send({ error: 'CSRF token missing or invalid' })
        }
    })

    app.addHook('preHandler', async (request, reply) => {
        const isTelemetryRead =
            request.url.startsWith('/api/v1/telemetry/stream') ||
            request.url.startsWith('/api/v1/telemetry/query')
        if (!isTelemetryRead || request.headers.authorization) return

        const access = request.cookies?.[config.accessCookieName]
        if (access && !isJwtExpired(access)) {
            request.headers.authorization = `Bearer ${access}`
            return
        }

        const refreshed = await refreshAccessToken(request, reply, config)
        if (refreshed) request.headers.authorization = `Bearer ${refreshed}`
        else if (access) request.headers.authorization = `Bearer ${access}`
    })
}, { name: 'session-security', dependencies: ['request-context'] })
