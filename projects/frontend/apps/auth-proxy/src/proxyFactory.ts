/**
 * proxyFactory.ts
 *
 * Factory helper for the common @fastify/http-proxy registration pattern used in auth-proxy.
 *
 * The "standard" pattern shared by several proxy registrations:
 *   1. Copy all incoming headers, flattening arrays to their first element.
 *   2. Inject X-Trace-Id (preserved from the client) and a fresh X-Request-Id.
 *   3. Inject `Authorization: Bearer <access_token>` from the session cookie.
 *   4. Optionally delete the `cookie` header so it is never forwarded downstream.
 *   5. Optionally ensure `content-type: application/json` for POST/PUT/PATCH.
 *
 * Registrations with extra logic (e.g. RBAC header injection, X-User-Id decoding,
 * telemetry stream/query special-casing, verbose per-registration logging, or
 * websocket: true) are intentionally NOT covered by this factory and must remain
 * as explicit app.register(httpProxy, {...}) calls with inline comments.
 */

import httpProxy from '@fastify/http-proxy'
import type { FastifyInstance } from 'fastify'
import { randomUUID } from 'crypto'

// ---------------------------------------------------------------------------
// Internal helpers (duplicated from index.ts to avoid circular imports)
// ---------------------------------------------------------------------------

/**
 * Normalises a UUID string by stripping dashes.
 * Returns undefined when the input is falsy.
 */
function normalizeUUID(uuid: string | undefined): string | undefined {
    return uuid ? uuid.replace(/-/g, '') : undefined
}

function generateUUID(): string {
    return randomUUID().replace(/-/g, '')
}

/**
 * Parses a raw `Cookie` header value into a key→value map.
 * Identical to the implementation in index.ts; kept here to avoid a circular
 * import.  If the cookie-parsing logic ever changes, update both files.
 */
export function parseCookiesLocal(header: string | undefined): Record<string, string> {
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

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

export interface RegisterAuthProxyOptions {
    /** Prefix path that Fastify will match (e.g. '/api/v1/users'). */
    prefix: string
    /** Full upstream base URL (e.g. 'http://auth-service:8001'). */
    upstream: string
    /**
     * Rewrite prefix forwarded to the upstream (defaults to `prefix` when
     * not provided — i.e. paths are forwarded verbatim).
     */
    rewritePrefix?: string
    /**
     * Cookie name that holds the user's access token.
     * Defaults to 'access_token'.
     */
    accessCookieName?: string
    /**
     * When true (default) the `cookie` header is stripped before the request
     * reaches the upstream service.
     * Set to false only if the upstream genuinely needs to see cookies.
     */
    deleteCookie?: boolean
    /**
     * When true (default) a `content-type: application/json` header is injected
     * for POST / PUT / PATCH requests that do not already carry one.
     */
    ensureJsonContentType?: boolean
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

/**
 * Registers a standard cookie-authenticated proxy route on the given Fastify
 * instance.
 *
 * Suitable for proxy routes whose only bespoke behaviour is:
 *   - Bearer token injection from the session cookie
 *   - Trace / request-ID propagation
 *   - Optional cookie stripping
 *
 * Do NOT use for routes that require:
 *   - RBAC header injection (X-User-Is-Superadmin, X-User-Permissions, …)
 *   - X-User-Id extraction via JWT decoding
 *   - Telemetry-specific auth (stream/query session-cookie fallback)
 *   - WebSocket support (websocket: true)
 *   - Per-registration structured logging inside rewriteRequestHeaders
 *
 * Those routes must remain as explicit app.register(httpProxy, {...}) calls.
 */
export async function registerAuthProxy(
    app: FastifyInstance,
    opts: RegisterAuthProxyOptions
): Promise<void> {
    const {
        prefix,
        upstream,
        rewritePrefix = prefix,
        accessCookieName = 'access_token',
        deleteCookie = true,
        ensureJsonContentType = true,
    } = opts

    await app.register(httpProxy, {
        prefix,
        upstream,
        rewritePrefix,
        http2: false,
        replyOptions: {
            rewriteRequestHeaders: (req, headers) => {
                const cookies = parseCookiesLocal(req.headers.cookie as string | undefined)
                const access = cookies[accessCookieName]

                const traceId =
                    normalizeUUID(req.headers['x-trace-id'] as string | undefined) ??
                    generateUUID()
                const requestId = generateUUID()

                // Flatten headers: convert arrays to their first element, drop undefined.
                const newHeaders: Record<string, string> = {}
                for (const [key, value] of Object.entries(headers)) {
                    if (typeof value === 'string') {
                        newHeaders[key] = value
                    } else if (Array.isArray(value) && value.length > 0) {
                        newHeaders[key] = String(value[0])
                    }
                }

                // Ensure content-type for mutating methods when caller opts in.
                if (
                    ensureJsonContentType &&
                    !newHeaders['content-type'] &&
                    (req.method === 'POST' || req.method === 'PUT' || req.method === 'PATCH')
                ) {
                    newHeaders['content-type'] = 'application/json'
                }

                // Standard tracing headers (delete incoming lowercase variants before overriding).
                delete newHeaders['x-trace-id']
                delete newHeaders['x-request-id']
                newHeaders['X-Trace-Id'] = traceId
                newHeaders['X-Request-Id'] = requestId

                // Inject bearer token when present.
                if (access) {
                    newHeaders['authorization'] = `Bearer ${access}`
                }

                // Strip the cookie header so session cookies are never leaked to
                // downstream services.
                if (deleteCookie) {
                    delete newHeaders['cookie']
                }

                return newHeaders
            },
        },
    })
}
