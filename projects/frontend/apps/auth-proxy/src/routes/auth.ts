import type { FastifyPluginAsync } from 'fastify'
import type { Config } from '../config'
import { buildAuthRateLimits } from '../rateLimits'
import {
    AuthTokens,
    clearAuthCookies,
    getOutgoingRequestHeaders,
    getTraceContext,
    setAuthCookies,
    setCsrfCookie,
} from '../security'

export interface AuthRoutesOptions {
    config: Config
}

export const registerAuthRoutes: FastifyPluginAsync<AuthRoutesOptions> = async (
    app,
    { config }
) => {
    // Per-route `config.rateLimit` is set explicitly on each authenticator/credential
    // handler so static analysis (CodeQL js/missing-rate-limiting) recognises that
    // the route is protected, and so we can apply stricter limits than the global
    // default for credential-handling endpoints (login/register).
    const { authLoginRateLimit, authRegisterRateLimit, authMutationRateLimit } =
        buildAuthRateLimits(config)

    app.post('/auth/login', { config: { rateLimit: authLoginRateLimit } }, async (request, reply) => {
        const { traceId } = getTraceContext(request)
        const outgoingHeaders = getOutgoingRequestHeaders(traceId)

        const res = await fetch(`${config.authUrl}/auth/login`, {
            method: 'POST',
            headers: {
                'content-type': 'application/json',
                ...outgoingHeaders,
            },
            body: JSON.stringify(request.body ?? {}),
        })

        if (!res.ok) {
            reply.status(res.status)
            return res.json().catch(() => ({}))
        }

        const data = (await res.json()) as AuthTokens
        if (!data.access_token) {
            reply.status(502)
            return { error: 'Auth service response missing access_token' }
        }

        setAuthCookies(reply, config, data)
        setCsrfCookie(reply, config)

        const { access_token, refresh_token, ...rest } = data
        // Forward password_change_required so the frontend can redirect to the
        // change-password page. Strip it from the object if it is falsy to
        // keep the response body lean.
        if (!rest.password_change_required) {
            delete rest.password_change_required
        }
        return rest
    })

    app.post('/auth/register', { config: { rateLimit: authRegisterRateLimit } }, async (request, reply) => {
        const { traceId } = getTraceContext(request)
        const outgoingHeaders = getOutgoingRequestHeaders(traceId)

        const res = await fetch(`${config.authUrl}/auth/register`, {
            method: 'POST',
            headers: {
                'content-type': 'application/json',
                ...outgoingHeaders,
            },
            body: JSON.stringify(request.body ?? {}),
        })

        if (!res.ok) {
            reply.status(res.status)
            return res.json().catch(() => ({}))
        }

        const data = (await res.json()) as AuthTokens
        if (!data.access_token) {
            reply.status(502)
            return { error: 'Auth service response missing access_token' }
        }

        setAuthCookies(reply, config, data)
        setCsrfCookie(reply, config)

        const { access_token, refresh_token, ...rest } = data
        return rest
    })

    app.post('/auth/refresh', { config: { rateLimit: authMutationRateLimit } }, async (request, reply) => {
        const refreshToken =
            request.cookies[config.refreshCookieName] ??
            (request.body as Record<string, unknown> | undefined)?.[
                config.refreshCookieName
            ]

        if (!refreshToken) {
            reply.status(401)
            return { error: 'Refresh token not provided' }
        }

        const { traceId } = getTraceContext(request)
        const outgoingHeaders = getOutgoingRequestHeaders(traceId)

        const res = await fetch(`${config.authUrl}/auth/refresh`, {
            method: 'POST',
            headers: {
                'content-type': 'application/json',
                ...outgoingHeaders,
            },
            body: JSON.stringify({ refresh_token: refreshToken }),
        })

        if (!res.ok) {
            clearAuthCookies(reply, config)
            reply.status(res.status)
            return res.json().catch(() => ({}))
        }

        const data = (await res.json()) as AuthTokens
        if (!data.access_token) {
            reply.status(502)
            return { error: 'Auth service response missing access_token' }
        }

        setAuthCookies(reply, config, data)
        setCsrfCookie(reply, config)
        const { access_token, refresh_token, ...rest } = data
        return rest
    })

    app.post('/auth/logout', { config: { rateLimit: authMutationRateLimit } }, async (request, reply) => {
        const { traceId, requestId } = getTraceContext(request)
        const outgoingHeaders = getOutgoingRequestHeaders(traceId)

        const refreshToken = request.cookies?.[config.refreshCookieName]

        // Best-effort revoke — always pass the refresh token so the auth-service
        // can invalidate the token family (rotation support).
        try {
            await fetch(`${config.authUrl}/auth/logout`, {
                method: 'POST',
                headers: {
                    'content-type': 'application/json',
                    ...outgoingHeaders,
                },
                body: JSON.stringify(
                    refreshToken ? { refresh_token: refreshToken } : {}
                ),
            })
        } catch (err) {
            request.log.warn(
                { err, trace_id: traceId, request_id: requestId },
                'Auth logout upstream failed'
            )
        }

        clearAuthCookies(reply, config)
        return { ok: true }
    })

    app.post('/auth/password-reset/request', { config: { rateLimit: authMutationRateLimit } }, async (request, reply) => {
        const { traceId } = getTraceContext(request)
        const outgoingHeaders = getOutgoingRequestHeaders(traceId)

        const res = await fetch(`${config.authUrl}/auth/password-reset/request`, {
            method: 'POST',
            headers: {
                'content-type': 'application/json',
                ...outgoingHeaders,
            },
            body: JSON.stringify(request.body ?? {}),
        })

        reply.status(res.status)
        return res.json().catch(() => ({}))
    })

    app.post('/auth/password-reset/confirm', { config: { rateLimit: authMutationRateLimit } }, async (request, reply) => {
        const { traceId } = getTraceContext(request)
        const outgoingHeaders = getOutgoingRequestHeaders(traceId)

        const res = await fetch(`${config.authUrl}/auth/password-reset/confirm`, {
            method: 'POST',
            headers: {
                'content-type': 'application/json',
                ...outgoingHeaders,
            },
            body: JSON.stringify(request.body ?? {}),
        })

        if (!res.ok) {
            reply.status(res.status)
            return res.json().catch(() => ({}))
        }

        const data = (await res.json()) as AuthTokens
        if (data.access_token) {
            setAuthCookies(reply, config, data)
            setCsrfCookie(reply, config)
            const { access_token, refresh_token, ...rest } = data
            return rest
        }

        return data
    })

    app.post('/auth/change-password', { config: { rateLimit: authMutationRateLimit } }, async (request, reply) => {
        let access = request.cookies[config.accessCookieName]
        let refreshedTokens: AuthTokens | undefined
        const refreshToken = request.cookies[config.refreshCookieName]

        const { traceId } = getTraceContext(request)
        const outgoingHeaders = getOutgoingRequestHeaders(traceId)

        const refreshSession = async (): Promise<
            | { tokens: AuthTokens }
            | { status: number; body: unknown; clearCookies: boolean }
        > => {
            if (!refreshToken) {
                return { status: 401, body: { error: 'Unauthorized' }, clearCookies: false }
            }

            const refreshRes = await fetch(`${config.authUrl}/auth/refresh`, {
                method: 'POST',
                headers: {
                    'content-type': 'application/json',
                    ...outgoingHeaders,
                },
                body: JSON.stringify({ refresh_token: refreshToken }),
            })

            if (!refreshRes.ok) {
                return {
                    status: refreshRes.status,
                    body: await refreshRes.json().catch(() => ({})),
                    clearCookies: true,
                }
            }

            const tokens = (await refreshRes.json()) as AuthTokens
            if (!tokens.access_token) {
                return {
                    status: 502,
                    body: { error: 'Auth service response missing access_token' },
                    clearCookies: false,
                }
            }

            return { tokens }
        }

        const respondRefreshFailure = (failure: {
            status: number
            body: unknown
            clearCookies: boolean
        }) => {
            if (failure.clearCookies) clearAuthCookies(reply, config)
            reply.status(failure.status)
            return failure.body
        }

        if (!access) {
            const refresh = await refreshSession()
            if (!('tokens' in refresh)) return respondRefreshFailure(refresh)
            refreshedTokens = refresh.tokens
            access = refreshedTokens.access_token
        }

        const changePassword = (token: string) => fetch(`${config.authUrl}/auth/change-password`, {
            method: 'POST',
            headers: {
                'content-type': 'application/json',
                authorization: `Bearer ${token}`,
                ...outgoingHeaders,
            },
            body: JSON.stringify(request.body ?? {}),
        })

        let res = await changePassword(access)
        if (res.status === 401 && !refreshedTokens && refreshToken) {
            const refresh = await refreshSession()
            if (!('tokens' in refresh)) return respondRefreshFailure(refresh)
            refreshedTokens = refresh.tokens
            res = await changePassword(refreshedTokens.access_token)
        }

        if (!res.ok) {
            if (refreshedTokens) {
                setAuthCookies(reply, config, refreshedTokens)
                setCsrfCookie(reply, config)
            }
            reply.status(res.status)
            return res.json().catch(() => ({}))
        }

        const data = (await res.json()) as AuthTokens
        if (!data.access_token) {
            if (refreshedTokens) {
                setAuthCookies(reply, config, refreshedTokens)
                setCsrfCookie(reply, config)
            }
            reply.status(502)
            return { error: 'Auth service response missing access_token' }
        }

        setAuthCookies(reply, config, data)
        setCsrfCookie(reply, config)
        const { access_token, refresh_token, ...rest } = data
        return rest
    })

    app.get('/auth/me', { config: { rateLimit: authMutationRateLimit } }, async (request, reply) => {
        const access = request.cookies[config.accessCookieName]
        if (!access) {
            reply.status(401)
            return { error: 'Unauthorized' }
        }

        const { traceId } = getTraceContext(request)
        const outgoingHeaders = getOutgoingRequestHeaders(traceId)

        const res = await fetch(`${config.authUrl}/auth/me`, {
            headers: {
                authorization: `Bearer ${access}`,
                ...outgoingHeaders,
            },
        })

        if (!res.ok) {
            if (res.status === 401) {
                clearAuthCookies(reply, config)
            }
            reply.status(res.status)
            return res.json().catch(() => ({}))
        }

        return res.json().catch(() => ({}))
    })
}
