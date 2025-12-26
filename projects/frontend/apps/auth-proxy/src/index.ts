import 'dotenv/config'
import fastify, { FastifyReply, FastifyRequest } from 'fastify'
import cors from '@fastify/cors'
import cookie from '@fastify/cookie'
import rateLimit from '@fastify/rate-limit'
import httpProxy from '@fastify/http-proxy'
import { randomUUID } from 'crypto'

type Config = {
    port: number
    targetExperimentUrl: string
    authUrl: string
    corsOrigins: string[]
    cookieDomain?: string
    cookieSecure: boolean
    cookieSameSite: 'lax' | 'strict' | 'none'
    accessCookieName: string
    refreshCookieName: string
    accessTtlSec: number
    refreshTtlSec: number
    rateLimitWindowMs: number
    rateLimitMax: number
    logLevel: string
}

function parseConfig(): Config {
    const num = (value: string | undefined, fallback: number) => {
        const parsed = value ? Number(value) : NaN
        return Number.isFinite(parsed) ? parsed : fallback
    }

    const bool = (value: string | undefined, fallback: boolean) => {
        if (value === undefined) return fallback
        return ['1', 'true', 'yes', 'on'].includes(value.toLowerCase())
    }

    return {
        port: num(process.env.PORT, 8080),
        targetExperimentUrl:
            process.env.TARGET_EXPERIMENT_URL || 'http://localhost:8002',
        authUrl: process.env.AUTH_URL || 'http://localhost:8001',
        corsOrigins: (process.env.CORS_ORIGINS || 'http://localhost:3000')
            .split(',')
            .map((o) => o.trim())
            .filter(Boolean),
        cookieDomain: process.env.COOKIE_DOMAIN,
        cookieSecure: bool(process.env.COOKIE_SECURE, false),
        cookieSameSite:
            (process.env.COOKIE_SAMESITE as Config['cookieSameSite']) || 'lax',
        accessCookieName: process.env.ACCESS_COOKIE_NAME || 'access_token',
        refreshCookieName: process.env.REFRESH_COOKIE_NAME || 'refresh_token',
        accessTtlSec: num(process.env.ACCESS_TTL_SEC, 900),
        refreshTtlSec: num(process.env.REFRESH_TTL_SEC, 1_209_600),
        rateLimitWindowMs: num(process.env.RATE_LIMIT_WINDOW_MS, 60_000),
        rateLimitMax: num(process.env.RATE_LIMIT_MAX, 60),
        logLevel: process.env.LOG_LEVEL || 'info',
    }
}

type AuthTokens = {
    access_token: string
    refresh_token?: string
    expires_in?: number
    refresh_expires_in?: number
    token_type?: string
    [key: string]: unknown
}

function parseCookies(header: string | undefined): Record<string, string> {
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

function setAuthCookies(
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

function clearAuthCookies(reply: FastifyReply, cfg: Config) {
    reply.clearCookie(cfg.accessCookieName, { path: '/' })
    reply.clearCookie(cfg.refreshCookieName, { path: '/' })
}

/**
 * Генерирует UUID без дефисов
 */
function generateUUID(): string {
    return randomUUID().replace(/-/g, '')
}

/**
 * Нормализует UUID, убирая дефисы (если есть)
 */
function normalizeUUID(uuid: string | undefined): string | undefined {
    return uuid ? uuid.replace(/-/g, '') : undefined
}

/**
 * Извлекает trace_id из заголовков (или генерирует новый для запроса от фронтенда)
 * Генерирует новый request_id для каждого запроса в auth-proxy
 */
function getTraceContext(request: FastifyRequest): {
    traceId: string
    requestId: string
} {
    // trace_id должен быть уникален для каждого запроса от фронтенда
    // извлекаем из заголовков (нормализуем, убирая дефисы) или генерируем новый
    const traceId = normalizeUUID(request.headers['x-trace-id'] as string) || generateUUID()
    // request_id должен быть уникален для каждого запроса в каждом сервисе
    // всегда генерируем новый для auth-proxy
    const requestId = generateUUID()
    return { traceId, requestId }
}

/**
 * Генерирует новый request_id для исходящего запроса к другому сервису
 * trace_id передается без изменений
 */
function getOutgoingRequestHeaders(traceId: string): {
    'X-Trace-Id': string
    'X-Request-Id': string
} {
    return {
        'X-Trace-Id': traceId,
        'X-Request-Id': generateUUID(), // новый request_id для каждого исходящего запроса
    }
}

async function buildServer(config: Config) {
    const app = fastify({
        logger: {
            level: config.logLevel,
            serializers: {
                req: (req) => {
                    const { traceId, requestId } = getTraceContext(req as FastifyRequest)
                    return {
                        method: req.method,
                        url: req.url,
                        trace_id: traceId,
                        request_id: requestId,
                    }
                },
                res: (res) => {
                    return {
                        statusCode: res.statusCode,
                    }
                },
            },
            redact: [
                'req.headers.authorization',
                'request.headers.authorization',
                'req.headers.cookie',
                'request.headers.cookie',
                'reply.headers.set-cookie',
                'response.headers.set-cookie',
            ],
        },
        trustProxy: true,
    })

    await app.register(cookie)

    await app.register(cors, {
        origin: config.corsOrigins,
        credentials: true,
    })

    await app.register(rateLimit, {
        max: config.rateLimitMax,
        timeWindow: config.rateLimitWindowMs,
        allowList: (req) => req.url === '/health',
    })

    // Middleware для извлечения и логирования trace_id и request_id
    app.addHook('onRequest', async (request, reply) => {
        const { traceId, requestId } = getTraceContext(request)

            // Сохраняем в request для использования в обработчиках
            ; (request as any).traceId = traceId
            ; (request as any).requestId = requestId

        // Добавляем trace_id и request_id в контекст логгера для всех последующих логов
        request.log = request.log.child({
            trace_id: traceId,
            request_id: requestId,
            service: 'auth-proxy',
        })

        // Логирование запроса с trace_id и request_id
        request.log.info({
            method: request.method,
            url: request.url,
        }, 'Incoming request')
    })

    // Hook для логирования ответов
    app.addHook('onResponse', async (request, reply) => {
        const logData: Record<string, unknown> = {
            method: request.method,
            url: request.url,
            statusCode: reply.statusCode,
        }

        // Логируем ошибки с дополнительной информацией
        if (reply.statusCode >= 400) {
            logData.error = 'Request failed'
        }

        request.log.info(logData, 'Request completed')
    })

    // Hook для логирования ошибок прокси
    app.addHook('onError', async (request, reply, error) => {
        request.log.error({
            method: request.method,
            url: request.url,
            error: error.message,
            stack: error.stack,
        }, 'Proxy error')
    })

    // Auth routes (login/refresh/logout/me) — устанавливают куки
    app.post('/auth/login', async (request, reply) => {
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

        const { access_token, refresh_token, ...rest } = data
        return rest
    })

    app.post('/auth/refresh', async (request, reply) => {
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
        const { access_token, refresh_token, ...rest } = data
        return rest
    })

    app.post('/auth/logout', async (request, reply) => {
        const { traceId, requestId } = getTraceContext(request)
        const outgoingHeaders = getOutgoingRequestHeaders(traceId)

        // Best-effort revoke
        try {
            await fetch(`${config.authUrl}/auth/logout`, {
                method: 'POST',
                headers: {
                    'content-type': 'application/json',
                    ...outgoingHeaders,
                },
                body: JSON.stringify({}),
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

    app.get('/auth/me', async (request, reply) => {
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

    /**
     * Декодирует JWT токен и извлекает user_id
     */
    function decodeJWT(token: string): { user_id?: string } | null {
        try {
            // JWT состоит из трех частей: header.payload.signature
            const parts = token.split('.')
            if (parts.length !== 3) return null

            // Декодируем payload (base64url)
            const payload = parts[1]
            const decoded = Buffer.from(payload, 'base64url').toString('utf-8')
            const parsed = JSON.parse(decoded)

            // JWT payload содержит sub (subject) с user_id
            return { user_id: parsed.sub || parsed.user_id }
        } catch (err) {
            return null
        }
    }

    /**
     * Извлекает project_id из query параметров
     * Примечание: body недоступен в rewriteRequestHeaders, поэтому используем только query
     */
    function extractProjectId(req: FastifyRequest): string | null {
        // Пытаемся получить из query параметров
        const query = req.query as Record<string, string | string[]> | undefined
        if (query?.project_id) {
            const projectId = Array.isArray(query.project_id)
                ? query.project_id[0]
                : query.project_id
            return projectId
        }

        return null
    }

    // Hook для извлечения project_id из body для POST/PUT/PATCH запросов
    app.addHook('preHandler', async (request, reply) => {
        // Для запросов к /api/* пытаемся извлечь project_id из body
        if (request.url.startsWith('/api/') && request.body) {
            const body = request.body as Record<string, unknown> | undefined
            if (body?.project_id && typeof body.project_id === 'string') {
                // Сохраняем project_id в request для использования в rewriteRequestHeaders
                ; (request as any).bodyProjectId = body.project_id
            }
        }
    })

    // Projects proxy - forward /projects/* to Auth Service
    await app.register(httpProxy, {
        prefix: '/projects',
        upstream: config.authUrl,
        rewritePrefix: '/projects',
        http2: false,
        replyOptions: {
            rewriteRequestHeaders: (req, headers) => {
                const cookies = parseCookies(req.headers.cookie as string | undefined)
                const access = cookies[config.accessCookieName]
                // trace_id передается от фронтенда, извлекаем из заголовков (нормализуем) или генерируем новый
                const traceId = normalizeUUID(req.headers['x-trace-id'] as string) || generateUUID()
                // Генерируем новый request_id для каждого исходящего запроса
                const outgoingHeaders = getOutgoingRequestHeaders(traceId)

                // Создаем новый объект заголовков, фильтруя массивы
                const newHeaders: Record<string, string> = {}

                // Копируем существующие заголовки (только строковые значения)
                for (const [key, value] of Object.entries(headers)) {
                    if (typeof value === 'string') {
                        newHeaders[key] = value
                    } else if (Array.isArray(value) && value.length > 0) {
                        // Берем первое значение из массива
                        newHeaders[key] = String(value[0])
                    }
                }

                // Убеждаемся, что Content-Type установлен для POST/PUT/PATCH запросов
                if (!newHeaders['content-type'] && (req.method === 'POST' || req.method === 'PUT' || req.method === 'PATCH')) {
                    newHeaders['content-type'] = 'application/json'
                }

                // Добавляем обязательные заголовки (trace_id передается, request_id генерируется новый)
                newHeaders['X-Trace-Id'] = outgoingHeaders['X-Trace-Id']
                newHeaders['X-Request-Id'] = outgoingHeaders['X-Request-Id']

                // Добавляем Authorization если есть токен
                if (access) {
                    newHeaders['authorization'] = `Bearer ${access}`
                }

                return newHeaders
            },
        },
    })

    // Experiment service proxy (+future gateway), with WS/SSE
    await app.register(httpProxy, {
        prefix: '/api',
        upstream: config.targetExperimentUrl,
        rewritePrefix: '/api',
        http2: false,
        websocket: true,
        replyOptions: {
            rewriteRequestHeaders: (req, headers) => {
                const cookies = parseCookies(req.headers.cookie as string | undefined)
                const access = cookies[config.accessCookieName]
                // trace_id передается от фронтенда, извлекаем из заголовков (нормализуем) или генерируем новый
                const traceId = normalizeUUID(req.headers['x-trace-id'] as string) || generateUUID()
                // Генерируем новый request_id для каждого исходящего запроса
                const outgoingHeaders = getOutgoingRequestHeaders(traceId)

                // Создаем новый объект заголовков, фильтруя массивы
                const newHeaders: Record<string, string> = {}

                // Копируем существующие заголовки (только строковые значения)
                for (const [key, value] of Object.entries(headers)) {
                    if (typeof value === 'string') {
                        newHeaders[key] = value
                    } else if (Array.isArray(value) && value.length > 0) {
                        // Берем первое значение из массива
                        newHeaders[key] = String(value[0])
                    }
                }

                // Добавляем обязательные заголовки (trace_id передается, request_id генерируется новый)
                newHeaders['X-Trace-Id'] = outgoingHeaders['X-Trace-Id']
                newHeaders['X-Request-Id'] = outgoingHeaders['X-Request-Id']

                // Добавляем Authorization если есть токен
                if (access) {
                    newHeaders['authorization'] = `Bearer ${access}`

                    // Декодируем JWT для получения user_id
                    const decoded = decodeJWT(access)
                    if (decoded?.user_id) {
                        newHeaders['X-User-Id'] = decoded.user_id
                    }
                }

                // Извлекаем project_id из запроса (query параметры или body)
                let projectId = extractProjectId(req as FastifyRequest)

                // Если не нашли в query, пытаемся извлечь из body (для POST/PUT/PATCH)
                if (!projectId) {
                    const bodyProjectId = (req as any).bodyProjectId
                    if (bodyProjectId && typeof bodyProjectId === 'string') {
                        projectId = bodyProjectId
                    }
                }

                // Добавляем project_id в заголовки только если он найден
                // Если не найден, Experiment Service будет требовать его в query/body
                if (projectId) {
                    newHeaders['X-Project-Id'] = projectId
                    // По умолчанию роль owner, можно будет улучшить позже
                    newHeaders['X-Project-Role'] = 'owner'
                }

                return newHeaders
            },
        },
    })

    app.get('/health', async () => ({ status: 'ok' }))

    return app
}

async function start() {
    const config = parseConfig()
    const app = await buildServer(config)

    try {
        await app.listen({ port: config.port, host: '0.0.0.0' })
        app.log.info(
            { port: config.port, upstream: config.targetExperimentUrl },
            'Auth proxy started'
        )
    } catch (err) {
        app.log.error(err, 'Failed to start auth proxy')
        process.exit(1)
    }
}

start()
