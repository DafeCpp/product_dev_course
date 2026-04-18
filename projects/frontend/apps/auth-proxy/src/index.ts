import 'dotenv/config'
import fastify, { FastifyReply, FastifyRequest } from 'fastify'
import cors from '@fastify/cors'
import cookie from '@fastify/cookie'
import rateLimit from '@fastify/rate-limit'
import httpProxy from '@fastify/http-proxy'
import { randomUUID } from 'crypto'
import { PassThrough } from 'stream'
import Redis from 'ioredis'

type Config = {
    port: number
    targetExperimentUrl: string
    targetTelemetryUrl: string
    targetScriptUrl: string
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
    redisUrl?: string
}

export function parseConfig(): Config {
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
        targetTelemetryUrl:
            // In Docker, `localhost` would mean "inside auth-proxy container".
            // Default to the docker-compose service name so telemetry proxy works out of the box.
            process.env.TARGET_TELEMETRY_URL || 'http://telemetry-ingest-service:8003',
        targetScriptUrl:
            process.env.TARGET_SCRIPT_URL || 'http://script-service:8004',
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
        redisUrl: process.env.REDIS_URL || undefined,
    }
}

// ---------------------------------------------------------------------------
// Permissions cache interface + implementations
// ---------------------------------------------------------------------------

interface EffectivePermissions {
    user_id: string
    is_superadmin: boolean
    system_permissions: string[]
    project_permissions: string[]
}

export interface PermissionsCache {
    get(key: string): Promise<EffectivePermissions | null>
    set(key: string, value: EffectivePermissions, ttlSec: number): Promise<void>
    invalidateUser(userId: string): Promise<void>
}

class NoopPermissionsCache implements PermissionsCache {
    get = async (_key: string): Promise<EffectivePermissions | null> => null
    set = async (_key: string, _value: EffectivePermissions, _ttlSec: number): Promise<void> => {}
    invalidateUser = async (_userId: string): Promise<void> => {}
}

export class InMemoryPermissionsCache implements PermissionsCache {
    private store = new Map<string, { value: EffectivePermissions; expiresAt: number }>()

    async get(key: string): Promise<EffectivePermissions | null> {
        const entry = this.store.get(key)
        if (!entry || Date.now() >= entry.expiresAt) return null
        return entry.value
    }

    async set(key: string, value: EffectivePermissions, ttlSec: number): Promise<void> {
        this.store.set(key, { value, expiresAt: Date.now() + ttlSec * 1000 })
    }

    async invalidateUser(userId: string): Promise<void> {
        for (const key of [...this.store.keys()]) {
            if (key.startsWith(`perms:${userId}`) || key === `perms:sys:${userId}`) {
                this.store.delete(key)
            }
        }
    }

    clear(): void {
        this.store.clear()
    }
}

class RedisPermissionsCache implements PermissionsCache {
    constructor(private readonly redis: Redis) {}

    async get(key: string): Promise<EffectivePermissions | null> {
        try {
            const raw = await this.redis.get(key)
            if (!raw) return null
            return JSON.parse(raw) as EffectivePermissions
        } catch {
            return null
        }
    }

    async set(key: string, value: EffectivePermissions, ttlSec: number): Promise<void> {
        try {
            await this.redis.setex(key, ttlSec, JSON.stringify(value))
        } catch {
            // ignore — graceful degradation
        }
    }

    async invalidateUser(userId: string): Promise<void> {
        try {
            const sysKey = `perms:sys:${userId}`
            const keysToDelete: string[] = [sysKey]
            const stream = this.redis.scanStream({ match: `perms:${userId}:*`, count: 100 })
            await new Promise<void>((resolve, reject) => {
                stream.on('data', (keys: string[]) => keysToDelete.push(...keys))
                stream.once('end', resolve)
                stream.once('error', reject)
            })
            if (keysToDelete.length > 0) {
                await this.redis.del(...keysToDelete)
            }
        } catch {
            // ignore
        }
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

function setCsrfCookie(reply: FastifyReply, cfg: Config) {
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
export function getTraceContext(request: FastifyRequest): {
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
export function getOutgoingRequestHeaders(traceId: string): {
    'X-Trace-Id': string
    'X-Request-Id': string
} {
    return {
        'X-Trace-Id': traceId,
        'X-Request-Id': generateUUID(), // новый request_id для каждого исходящего запроса
    }
}

function _decodeJwtPayload(token: string): Record<string, unknown> | null {
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

function _isJwtExpired(token: string, skewSec = 30): boolean {
    const payload = _decodeJwtPayload(token)
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

export async function buildServer(config: Config, _cache?: PermissionsCache) {
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

    // ---------------------------------------------------------------------------
    // Permissions cache setup
    // ---------------------------------------------------------------------------
    const PERM_TTL_SEC = 30

    let permCache: PermissionsCache

    if (_cache !== undefined) {
        // DI — used in tests
        permCache = _cache
    } else if (config.redisUrl) {
        const redisClient = new Redis(config.redisUrl, {
            maxRetriesPerRequest: 1,
            enableReadyCheck: false,
            lazyConnect: true,
        })
        redisClient.on('error', (err: Error) => {
            app.log.warn({ err: err.message }, 'Redis connection error — permissions cache disabled')
        })
        try {
            await redisClient.connect()
            permCache = new RedisPermissionsCache(redisClient)
            app.addHook('onClose', async () => {
                await redisClient.quit().catch(() => {})
            })
        } catch (err) {
            app.log.warn({ err: String(err) }, 'Failed to connect to Redis — running without cache')
            permCache = new NoopPermissionsCache()
        }
    } else {
        permCache = new NoopPermissionsCache()
    }

    function _permsCacheKey(userId: string, projectId?: string): string {
        return projectId ? `perms:${userId}:${projectId}` : `perms:sys:${userId}`
    }

    await app.register(cookie)

    await app.register(cors, {
        origin: config.corsOrigins,
        credentials: true,
        // IMPORTANT:
        // Portal (http://localhost:3000) делает state-changing запросы (PUT/PATCH/DELETE) через auth-proxy.
        // Браузер отправляет CORS preflight (OPTIONS) и требует, чтобы Access-Control-Allow-Methods
        // включал нужный метод, иначе запрос блокируется (например, PATCH).
        methods: ['GET', 'HEAD', 'POST', 'PUT', 'PATCH', 'DELETE', 'OPTIONS'],
        allowedHeaders: [
            'Accept',
            'Accept-Language',
            'Content-Language',
            'Content-Type',
            'Authorization',
            // tracing
            'X-Trace-Id',
            'X-Request-Id',
            // CSRF (double-submit)
            'X-CSRF-Token',
            // project context headers
            'X-Project-Id',
            'X-User-Id',
        ],
        // Expose some headers for debugging (optional).
        exposedHeaders: ['X-Trace-Id', 'X-Request-Id'],
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

        // SSE hardening: disable timeouts/buffering and set streaming headers early.
        const accept = String(request.headers.accept || '')
        const url = String(request.url || '')
        const isSse =
            accept.includes('text/event-stream') ||
            url.startsWith('/api/v1/telemetry/stream')
        if (isSse) {
            try {
                if (typeof request.raw.setTimeout === 'function') {
                    request.raw.setTimeout(0)
                }
                // Keep the socket open for streaming.
                if (typeof request.raw.socket?.setTimeout === 'function') {
                    request.raw.socket.setTimeout(0)
                }
                if (typeof reply.raw.setTimeout === 'function') {
                    reply.raw.setTimeout(0)
                }
            } catch (err) {
                request.log.warn({ err }, 'SSE hardening failed')
            }
        }
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

    function _originAllowed(origin: string | undefined): boolean {
        if (!origin) return false
        const normalized = origin.toLowerCase()
        return config.corsOrigins.some((o) => o.toLowerCase() === normalized)
    }

    function _extractOriginFromReferer(referer: string | undefined): string | undefined {
        if (!referer) return undefined
        try {
            return new URL(referer).origin
        } catch {
            return undefined
        }
    }

    async function _refreshAccessTokenForRequest(
        request: FastifyRequest,
        reply: FastifyReply
    ): Promise<string | null> {
        const refreshToken = request.cookies?.[config.refreshCookieName]
        if (!refreshToken) return null

        const traceId =
            (request as any).traceId ||
            normalizeUUID(request.headers['x-trace-id'] as string) ||
            generateUUID()
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
            return null
        }

        const data = (await res.json()) as AuthTokens
        if (!data.access_token) return null

        setAuthCookies(reply, config, data)
        setCsrfCookie(reply, config)
        return data.access_token
    }

    // CSRF protection for cookie-authenticated, state-changing requests (double-submit cookie).
    // Exclusions:
    //  - /auth/login, /auth/register, and /auth/refresh: no CSRF cookie yet
    //  - /api/v1/telemetry/*: auth is via Authorization sensor token (not cookies)
    app.addHook('preHandler', async (request, reply) => {
        const method = (request.method || '').toUpperCase()
        const isStateChanging = ['POST', 'PUT', 'PATCH', 'DELETE'].includes(method)
        if (!isStateChanging) return

        const url = request.url || ''
        if (url === '/health') return
        if (
            url.startsWith('/auth/login') ||
            url.startsWith('/auth/register') ||
            url.startsWith('/auth/refresh') ||
            url.startsWith('/auth/password-reset')
        )
            return
        if (url.startsWith('/api/v1/telemetry')) return

        const hasSessionCookie = Boolean(
            request.cookies?.[config.accessCookieName] || request.cookies?.[config.refreshCookieName]
        )
        if (!hasSessionCookie) return

        const origin = (request.headers.origin as string | undefined) || undefined
        const referer = (request.headers.referer as string | undefined) || undefined
        const originToCheck = origin || _extractOriginFromReferer(referer)
        if (!originToCheck || !_originAllowed(originToCheck)) {
            reply.status(403).send({ error: 'CSRF origin missing or invalid' })
            return
        }

        const csrfCookie = request.cookies?.csrf_token
        const hdr = request.headers['x-csrf-token'] as string | string[] | undefined
        const csrfHeader = Array.isArray(hdr) ? hdr[0] : hdr

        if (!csrfCookie || !csrfHeader || csrfCookie !== csrfHeader) {
            reply.status(403).send({ error: 'CSRF token missing or invalid' })
            return
        }
    })

    // Ensure telemetry read requests always carry a valid bearer token.
    app.addHook('preHandler', async (request, reply) => {
        const url = request.url || ''
        const isTelemetryRead =
            url.startsWith('/api/v1/telemetry/stream') || url.startsWith('/api/v1/telemetry/query')
        if (!isTelemetryRead) return

        if (request.headers?.authorization) return

        const access = request.cookies?.[config.accessCookieName]
        if (access && !_isJwtExpired(access)) {
            ; (request.headers as any).authorization = `Bearer ${access}`
            return
        }

        const refreshed = await _refreshAccessTokenForRequest(request, reply)
        if (refreshed) {
            ; (request.headers as any).authorization = `Bearer ${refreshed}`
        } else if (access) {
            // Fall back to the existing access token if refresh failed.
            ; (request.headers as any).authorization = `Bearer ${access}`
        }
    })

    // Логирование ответов от проксированных запросов через отдельный hook
    // Это нужно, так как onResponse в http-proxy может блокировать ответ
    app.addHook('onSend', async (request, reply, payload) => {
        const accept = String(request.headers.accept || '')
        const url = String(request.url || '')
        const contentType = String(reply.getHeader('content-type') || '')
        const isSse =
            contentType.includes('text/event-stream') ||
            accept.includes('text/event-stream') ||
            url.startsWith('/api/v1/telemetry/stream')

        if (isSse && request.raw.socket && !reply.raw.headersSent) {
            reply.header('Cache-Control', 'no-cache, no-transform')
            reply.header('X-Accel-Buffering', 'no')
            if (!contentType) {
                reply.header('Content-Type', 'text/event-stream; charset=utf-8')
            }
        }

        // Инвалидация кэша прав при 401/403 от downstream (experiment-service)
        if ((reply.statusCode === 401 || reply.statusCode === 403) && request.url.startsWith('/api/')) {
            const cookies = parseCookies(request.headers.cookie as string | undefined)
            const access = cookies[config.accessCookieName]
            if (access) {
                const claims = getRbacClaimsFromJwt(access)
                if (claims?.user_id) {
                    // fire-and-forget: не блокируем ответ
                    permCache.invalidateUser(claims.user_id).catch(() => {})
                }
            }
        }

        // Логируем только проксированные запросы (не /health и не /auth/*)
        if (request.url.startsWith('/projects') || request.url.startsWith('/api/')) {
            const { traceId } = getTraceContext(request as FastifyRequest)
            app.log.debug({
                method: request.method,
                url: request.url,
                status_code: reply.statusCode,
                trace_id: traceId,
            }, 'Sending response to client')
        }
        return payload
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

    // Для /api/* маршрутов тело запроса проксируется как сырой поток (IncomingMessage),
    // поэтому request.body в preHandler недоступен как JSON-объект.
    // Перехватываем тело на этапе preParsing, буферизуем его целиком, сохраняем в
    // rawBodyForProjectId и возвращаем новый Readable с теми же данными.
    app.addHook('preParsing', async (request, _reply, payload) => {
        const method = (request.method || '').toUpperCase()
        if (!['POST', 'PUT', 'PATCH'].includes(method)) return payload
        if (!request.url?.startsWith('/api/')) return payload
        const contentType = String(request.headers['content-type'] || '').toLowerCase()
        if (!contentType.includes('application/json')) return payload

        const chunks: Buffer[] = []
        await new Promise<void>((resolve, reject) => {
            payload.on('data', (chunk: Buffer | string) => {
                chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk))
            })
            payload.once('end', resolve)
            payload.once('error', reject)
        })

        const bodyBuf = Buffer.concat(chunks)
        ;(request as any).rawBodyForProjectId = bodyBuf.toString('utf-8')

        // Возвращаем новый поток с теми же данными для дальнейшей обработки
        const newPayload = new PassThrough()
        newPayload.end(bodyBuf)
        return newPayload
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

    app.post('/auth/register', async (request, reply) => {
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
        setCsrfCookie(reply, config)
        const { access_token, refresh_token, ...rest } = data
        return rest
    })

    app.post('/auth/logout', async (request, reply) => {
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

    // Password reset routes — public, no auth/CSRF required
    app.post('/auth/password-reset/request', async (request, reply) => {
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

    app.post('/auth/password-reset/confirm', async (request, reply) => {
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

    // Admin routes proxy — forward /auth/admin/* to Auth Service with access token from cookie
    await app.register(httpProxy, {
        prefix: '/auth/admin',
        upstream: config.authUrl,
        rewritePrefix: '/auth/admin',
        http2: false,
        replyOptions: {
            rewriteRequestHeaders: (req, headers) => {
                const cookies = parseCookies(req.headers.cookie as string | undefined)
                const access = cookies[config.accessCookieName]
                const traceId = normalizeUUID(req.headers['x-trace-id'] as string) || generateUUID()
                const outgoingHeaders = getOutgoingRequestHeaders(traceId)

                const newHeaders: Record<string, string> = {}
                for (const [key, value] of Object.entries(headers)) {
                    if (typeof value === 'string') {
                        newHeaders[key] = value
                    } else if (Array.isArray(value) && value.length > 0) {
                        newHeaders[key] = String(value[0])
                    }
                }
                if (
                    !newHeaders['content-type'] &&
                    (req.method === 'POST' || req.method === 'PUT' || req.method === 'PATCH')
                ) {
                    newHeaders['content-type'] = 'application/json'
                }
                newHeaders['X-Trace-Id'] = outgoingHeaders['X-Trace-Id']
                newHeaders['X-Request-Id'] = outgoingHeaders['X-Request-Id']
                if (access) {
                    newHeaders['authorization'] = `Bearer ${access}`
                }
                return newHeaders
            },
        },
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

    /**
     * Извлекает project_id из тела запроса.
     * При использовании @fastify/http-proxy тело проксируется как сырой поток,
     * поэтому request.body недоступен как JSON — вместо него используем rawBodyForProjectId,
     * захваченный в preParsing hook.
     */
    function extractProjectIdFromBody(request: FastifyRequest): string | null {
        // Сырое тело, захваченное в preParsing (для proxy-маршрутов)
        const rawBody = (request as any).rawBodyForProjectId as string | undefined
        if (rawBody) {
            try {
                const parsed = JSON.parse(rawBody)
                const projectId = parsed?.project_id
                return typeof projectId === 'string' ? projectId : null
            } catch {
                return null
            }
        }

        // Fallback: request.body уже распарсен как объект (не proxy-маршруты)
        const body: unknown = request.body
        if (!body) return null
        if (
            typeof body === 'object' &&
            !Buffer.isBuffer(body) &&
            body !== null &&
            !Array.isArray(body)
        ) {
            const projectId = (body as any).project_id
            return typeof projectId === 'string' ? projectId : null
        }

        // Buffer-fallback
        const contentType = String(request.headers['content-type'] || '').toLowerCase()
        if (!contentType.includes('application/json')) return null
        try {
            const raw = Buffer.isBuffer(body) ? body.toString('utf-8') : String(body)
            if (!raw) return null
            const parsed = JSON.parse(raw) as any
            const projectId = parsed?.project_id
            return typeof projectId === 'string' ? projectId : null
        } catch {
            return null
        }
    }

    async function getEffectivePermissions(
        userId: string,
        accessToken: string,
        projectId?: string
    ): Promise<EffectivePermissions | null> {
        const cacheKey = _permsCacheKey(userId, projectId)

        const cached = await permCache.get(cacheKey)
        if (cached) return cached

        try {
            const url = projectId
                ? `${config.authUrl}/api/v1/users/${userId}/effective-permissions?project_id=${projectId}`
                : `${config.authUrl}/api/v1/users/${userId}/effective-permissions`

            const resp = await fetch(url, {
                headers: { Authorization: `Bearer ${accessToken}` },
                signal: AbortSignal.timeout(3000),
            })
            if (!resp.ok) return null
            const data = (await resp.json()) as EffectivePermissions

            await permCache.set(cacheKey, data, PERM_TTL_SEC)
            return data
        } catch (error) {
            app.log.error({
                user_id: userId,
                project_id: projectId,
                error: error instanceof Error ? error.message : String(error),
            }, 'Error fetching effective permissions')
            return null
        }
    }

    interface JwtRbacClaims {
        user_id: string
        is_superadmin: boolean
        system_permissions: string[]
    }

    function getRbacClaimsFromJwt(token: string): JwtRbacClaims | null {
        const payload = _decodeJwtPayload(token)
        if (!payload) return null
        const userId = (payload.sub ?? payload.user_id) as string | undefined
        if (!userId) return null
        return {
            user_id: userId,
            is_superadmin: (payload.sa as boolean) ?? false,
            system_permissions: (payload.sys as string[]) ?? [],
        }
    }

    // Hook для извлечения project_id из body или query для POST/PUT/PATCH запросов
    // и получения эффективных прав пользователя через auth-service
    app.addHook('preHandler', async (request, _reply) => {
        // Для запросов к /api/* пытаемся извлечь project_id из body или query
        if (request.url.startsWith('/api/')) {
            let projectId: string | null = null

            // Пытаемся получить project_id из body (для POST/PUT/PATCH)
            projectId = extractProjectIdFromBody(request)
            if (projectId) {
                // Сохраняем project_id в request для использования в rewriteRequestHeaders
                ; (request as any).bodyProjectId = projectId
            }

            // Если не нашли в body, пытаемся получить из query параметров
            if (!projectId) {
                projectId = extractProjectId(request as FastifyRequest)
            }

            const cookies = parseCookies(request.headers.cookie as string | undefined)
            const access = cookies[config.accessCookieName]

            if (access) {
                const rbacClaims = getRbacClaimsFromJwt(access)

                if (rbacClaims) {
                    if (rbacClaims.is_superadmin) {
                        // Суперадмин — не делаем HTTP-запрос, всё имплицитно
                        ; (request as any).permissionsIsSuperadmin = true
                        ; (request as any).permissionsSystemPerms = ''
                        ; (request as any).permissionsProjectPerms = ''
                    } else if (projectId) {
                        // Есть project_id — запрашиваем эффективные права
                        const perms = await getEffectivePermissions(rbacClaims.user_id, access, projectId)
                        ; (request as any).permissionsIsSuperadmin = false
                        ; (request as any).permissionsSystemPerms = perms
                            ? perms.system_permissions.join(',')
                            : rbacClaims.system_permissions.join(',')
                        ; (request as any).permissionsProjectPerms = perms
                            ? perms.project_permissions.join(',')
                            : ''
                        if (!perms) {
                            app.log.warn({
                                project_id: projectId,
                                user_id: rbacClaims.user_id,
                                url: request.url,
                            }, 'Failed to fetch effective permissions for project')
                        }
                    } else {
                        // Нет project_id — только системные права из JWT
                        ; (request as any).permissionsIsSuperadmin = false
                        ; (request as any).permissionsSystemPerms = rbacClaims.system_permissions.join(',')
                        ; (request as any).permissionsProjectPerms = ''
                    }
                }
            }

            // GET /api/v1/sensors без project_id: запрашиваем все проекты пользователя
            // и передаём их в experiment-service через X-Project-Ids
            if (
                !projectId &&
                (request.method === 'GET' || request.method === 'get') &&
                request.url.startsWith('/api/v1/sensors')
            ) {
                if (access) {
                    try {
                        const { traceId } = getTraceContext(request)
                        const outgoingHeaders = getOutgoingRequestHeaders(traceId)
                        const res = await fetch(`${config.authUrl}/projects`, {
                            method: 'GET',
                            headers: {
                                authorization: `Bearer ${access}`,
                                ...outgoingHeaders,
                            },
                        })
                        if (res.ok) {
                            const data = (await res.json()) as { projects?: Array<{ id: string }> }
                            const projects = data.projects ?? []
                                ; (request as any).allProjectIds = projects.map((p) => p.id)
                        }
                    } catch (err) {
                        app.log.warn(
                            { err: err instanceof Error ? err.message : String(err), url: request.url },
                            'Failed to fetch user projects for X-Project-Ids'
                        )
                    }
                }
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

                // Логируем входящий запрос к прокси
                // Используем app.log, так как в rewriteRequestHeaders нет прямого доступа к request.log
                const isStateChanging = ['POST', 'PUT', 'PATCH', 'DELETE'].includes(req.method || '')
                app.log.info({
                    method: req.method,
                    url: req.url,
                    upstream: config.authUrl,
                    has_auth_token: !!access,
                    is_state_changing: isStateChanging,
                    trace_id: outgoingHeaders['X-Trace-Id'],
                    request_id: outgoingHeaders['X-Request-Id'],
                }, 'Proxying request to Auth Service')

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

                // Логируем заголовки, которые отправляются (без чувствительных данных)
                app.log.debug({
                    method: req.method,
                    url: req.url,
                    headers_sent: Object.keys(newHeaders).filter(h => h.toLowerCase() !== 'authorization'),
                    has_authorization: !!newHeaders['authorization'],
                    content_type: newHeaders['content-type'] || undefined,
                }, 'Request headers prepared for Auth Service')

                return newHeaders
            },
        },
    })

    // Telemetry ingest proxy (SSE + REST ingest). This must be registered BEFORE /api proxy,
    // otherwise /api/* would forward telemetry endpoints to experiment-service.
    await app.register(httpProxy, {
        prefix: '/api/v1/telemetry',
        upstream: config.targetTelemetryUrl,
        rewritePrefix: '/api/v1/telemetry',
        http2: false,
        replyOptions: {
            rewriteRequestHeaders: (req, headers) => {
                // Telemetry endpoints:
                //  - POST /api/v1/telemetry: requires sensor token (Authorization: Bearer <sensor_token>)
                //  - GET  /api/v1/telemetry/stream: can be accessed either via sensor token OR via user session
                //    (we inject user's access token if client didn't provide Authorization)
                const traceId = normalizeUUID(req.headers['x-trace-id'] as string) || generateUUID()
                const outgoingHeaders = getOutgoingRequestHeaders(traceId)

                const newHeaders: Record<string, string> = {}
                for (const [key, value] of Object.entries(headers)) {
                    if (typeof value === 'string') newHeaders[key] = value
                    else if (Array.isArray(value) && value.length > 0) newHeaders[key] = String(value[0])
                }

                newHeaders['X-Trace-Id'] = outgoingHeaders['X-Trace-Id']
                newHeaders['X-Request-Id'] = outgoingHeaders['X-Request-Id']

                const url = String(req.url || '')
                const isTelemetryRead =
                    url.startsWith('/api/v1/telemetry/stream') || url.startsWith('/api/v1/telemetry/query')

                // If stream request has no Authorization, use session cookie access token
                if (isTelemetryRead && !newHeaders['authorization']) {
                    const cookies = parseCookies(req.headers.cookie as string | undefined)
                    const access = cookies[config.accessCookieName]
                    if (access) {
                        newHeaders['authorization'] = `Bearer ${access}`
                    }
                }

                // Ensure no cookie is forwarded to telemetry ingest service
                delete newHeaders['cookie']

                app.log.info({
                    method: req.method,
                    url: req.url,
                    upstream: config.targetTelemetryUrl,
                    has_authorization: !!newHeaders['authorization'],
                    trace_id: outgoingHeaders['X-Trace-Id'],
                    request_id: outgoingHeaders['X-Request-Id'],
                }, 'Proxying request to Telemetry Ingest Service')

                return newHeaders
            },
        },
    })

    // Script service proxy — /api/v1/scripts and /api/v1/executions → script-service
    for (const prefix of ['/api/v1/scripts', '/api/v1/executions']) {
        await app.register(httpProxy, {
            prefix,
            upstream: config.targetScriptUrl,
            rewritePrefix: prefix,
            http2: false,
            replyOptions: {
                rewriteRequestHeaders: (req, headers) => {
                    const cookies = parseCookies(req.headers.cookie as string | undefined)
                    const access = cookies[config.accessCookieName]
                    const traceId = normalizeUUID(req.headers['x-trace-id'] as string) || generateUUID()
                    const outgoingHeaders = getOutgoingRequestHeaders(traceId)
                    const newHeaders: Record<string, string> = {}
                    for (const [key, value] of Object.entries(headers)) {
                        if (typeof value === 'string') newHeaders[key] = value
                        else if (Array.isArray(value) && value.length > 0) newHeaders[key] = String(value[0])
                    }
                    newHeaders['X-Trace-Id'] = outgoingHeaders['X-Trace-Id']
                    newHeaders['X-Request-Id'] = outgoingHeaders['X-Request-Id']
                    if (access) {
                        newHeaders['authorization'] = `Bearer ${access}`
                        const decoded = decodeJWT(access)
                        if (decoded?.user_id) {
                            newHeaders['X-User-Id'] = decoded.user_id
                        }
                    }
                    const permIsSuperadmin = (req as any).permissionsIsSuperadmin
                    if (permIsSuperadmin !== undefined) {
                        newHeaders['X-User-Is-Superadmin'] = permIsSuperadmin ? 'true' : 'false'
                        newHeaders['X-User-System-Permissions'] = (req as any).permissionsSystemPerms ?? ''
                        newHeaders['X-User-Permissions'] = (req as any).permissionsProjectPerms ?? ''
                    }
                    delete newHeaders['cookie']
                    return newHeaders
                },
            },
        })
    }

    // Users API proxy — forward /api/v1/users/* to Auth Service (must be before generic /api proxy)
    await app.register(httpProxy, {
        prefix: '/api/v1/users',
        upstream: config.authUrl,
        rewritePrefix: '/api/v1/users',
        http2: false,
        replyOptions: {
            rewriteRequestHeaders: (req, headers) => {
                const cookies = parseCookies(req.headers.cookie as string | undefined)
                const access = cookies[config.accessCookieName]
                const traceId = normalizeUUID(req.headers['x-trace-id'] as string) || generateUUID()
                const outgoingHeaders = getOutgoingRequestHeaders(traceId)
                const newHeaders: Record<string, string> = {}
                for (const [key, value] of Object.entries(headers)) {
                    if (typeof value === 'string') newHeaders[key] = value
                    else if (Array.isArray(value) && value.length > 0) newHeaders[key] = String(value[0])
                }
                newHeaders['X-Trace-Id'] = outgoingHeaders['X-Trace-Id']
                newHeaders['X-Request-Id'] = outgoingHeaders['X-Request-Id']
                if (access) newHeaders['authorization'] = `Bearer ${access}`
                return newHeaders
            },
        },
    })

    // Auth Service API proxies — these endpoints live in auth-service, not experiment-service.
    // Must be registered BEFORE the generic `/api` proxy so they win route matching.
    for (const prefix of ['/api/v1/system-roles', '/api/v1/permissions', '/api/v1/audit-log']) {
        await app.register(httpProxy, {
            prefix,
            upstream: config.authUrl,
            rewritePrefix: prefix,
            http2: false,
            replyOptions: {
                rewriteRequestHeaders: (req, headers) => {
                    const cookies = parseCookies(req.headers.cookie as string | undefined)
                    const access = cookies[config.accessCookieName]
                    const traceId = normalizeUUID(req.headers['x-trace-id'] as string) || generateUUID()
                    const outgoingHeaders = getOutgoingRequestHeaders(traceId)
                    const newHeaders: Record<string, string> = {}
                    for (const [key, value] of Object.entries(headers)) {
                        if (typeof value === 'string') newHeaders[key] = value
                        else if (Array.isArray(value) && value.length > 0) newHeaders[key] = String(value[0])
                    }
                    newHeaders['X-Trace-Id'] = outgoingHeaders['X-Trace-Id']
                    newHeaders['X-Request-Id'] = outgoingHeaders['X-Request-Id']
                    if (access) newHeaders['authorization'] = `Bearer ${access}`
                    delete newHeaders['cookie']
                    return newHeaders
                },
            },
        })
    }

    // Sensor error log lives on telemetry-ingest-service, not experiment-service.
    // Must be registered BEFORE the generic `/api` proxy so it wins route matching.
    app.get<{ Params: { sensorId: string } }>(
        '/api/v1/sensors/:sensorId/error-log',
        async (req, reply) => {
            const cookies = parseCookies(req.headers.cookie as string | undefined)
            const access = cookies[config.accessCookieName]
            const traceId = normalizeUUID(req.headers['x-trace-id'] as string) || generateUUID()
            const outgoing = getOutgoingRequestHeaders(traceId)

            const incomingUrl = String(req.url || '')
            const queryIdx = incomingUrl.indexOf('?')
            const search = queryIdx >= 0 ? incomingUrl.slice(queryIdx) : ''
            const targetUrl =
                `${config.targetTelemetryUrl}/api/v1/sensors/${encodeURIComponent(req.params.sensorId)}/error-log${search}`

            const outHeaders: Record<string, string> = {
                'X-Trace-Id': outgoing['X-Trace-Id'],
                'X-Request-Id': outgoing['X-Request-Id'],
            }
            if (access) outHeaders['authorization'] = `Bearer ${access}`
            const accept = req.headers['accept']
            if (typeof accept === 'string') outHeaders['accept'] = accept

            app.log.info({
                method: 'GET',
                url: req.url,
                upstream: config.targetTelemetryUrl,
                has_authorization: !!outHeaders['authorization'],
                trace_id: outgoing['X-Trace-Id'],
                request_id: outgoing['X-Request-Id'],
            }, 'Proxying sensor error-log to Telemetry Ingest Service')

            const upstream = await fetch(targetUrl, { method: 'GET', headers: outHeaders })
            reply.code(upstream.status)
            const contentType = upstream.headers.get('content-type')
            if (contentType) reply.header('content-type', contentType)
            const body = await upstream.arrayBuffer()
            return reply.send(Buffer.from(body))
        }
    )

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

                // Извлекаем project_id из запроса (query параметры или body)
                let projectId = extractProjectId(req as FastifyRequest)

                // Если не нашли в query, пытаемся извлечь из body (для POST/PUT/PATCH)
                if (!projectId) {
                    const bodyProjectId = (req as any).bodyProjectId
                    if (bodyProjectId && typeof bodyProjectId === 'string') {
                        projectId = bodyProjectId
                    }
                }

                // Логируем входящий запрос к прокси
                app.log.info({
                    method: req.method,
                    url: req.url,
                    upstream: config.targetExperimentUrl,
                    has_auth_token: !!access,
                    has_project_id: !!projectId,
                    project_id: projectId || undefined,
                    trace_id: outgoingHeaders['X-Trace-Id'],
                    request_id: outgoingHeaders['X-Request-Id'],
                }, 'Proxying request to Experiment Service')

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

                // Инжектируем RBAC-заголовки из preHandler
                const permIsSuperadmin = (req as any).permissionsIsSuperadmin
                if (permIsSuperadmin !== undefined) {
                    newHeaders['X-User-Is-Superadmin'] = permIsSuperadmin ? 'true' : 'false'
                    newHeaders['X-User-System-Permissions'] = (req as any).permissionsSystemPerms ?? ''
                    newHeaders['X-User-Permissions'] = (req as any).permissionsProjectPerms ?? ''
                }

                // Список всех проектов пользователя (для GET /api/v1/sensors без project_id)
                const allProjectIds = (req as any).allProjectIds
                if (Array.isArray(allProjectIds) && allProjectIds.length > 0) {
                    newHeaders['X-Project-Ids'] = allProjectIds.join(',')
                }

                // Добавляем project_id в заголовки только если он найден
                if (projectId) {
                    newHeaders['X-Project-Id'] = projectId
                }

                // Логируем заголовки, которые отправляются (без чувствительных данных)
                app.log.debug({
                    method: req.method,
                    url: req.url,
                    headers_sent: Object.keys(newHeaders).filter(h =>
                        !['authorization', 'x-user-id'].includes(h.toLowerCase())
                    ),
                    has_authorization: !!newHeaders['authorization'],
                    has_user_id: !!newHeaders['X-User-Id'],
                    has_project_id: !!newHeaders['X-Project-Id'],
                }, 'Request headers prepared for Experiment Service')

                // Явное debug-логирование RBAC-заголовков
                app.log.debug({
                    method: req.method,
                    url: req.url,
                    x_project_id: newHeaders['X-Project-Id'] || undefined,
                    x_user_is_superadmin: newHeaders['X-User-Is-Superadmin'] || undefined,
                    has_x_project_id: !!newHeaders['X-Project-Id'],
                }, 'Auth proxy RBAC headers')

                return newHeaders
            },
            // Не используем onResponse hook, так как он может блокировать передачу ответа клиенту
            // Логирование ответов происходит через app.addHook('onSend')
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

// Важно: не запускаем сервер при импорте модуля (нужно для Jest-тестов)
if (require.main === module) {
    void start()
}
