import httpProxy from '@fastify/http-proxy'
import type { FastifyPluginAsync, FastifyRequest } from 'fastify'
import type { Config } from '../config'
import { extractProjectId } from '../plugins/permissions'
import {
    decodeJwtUserId,
    generateUUID,
    getOutgoingRequestHeaders,
    normalizeUUID,
    parseCookies,
} from '../security'
import { registerAuthProxy } from './factory'
import {
    buildProxyRouteTable,
    type ProxyRouteDefinition,
} from './routes'

interface ProxyRoutesOptions {
    config: Config
}

type HeaderValue = string | string[] | undefined
type ProxyRequest = Pick<FastifyRequest, 'headers' | 'url' | 'query'> & {
    bodyProjectId?: string
    permissionsIsSuperadmin?: boolean
    permissionsSystemPerms?: string
    permissionsProjectPerms?: string
    allProjectIds?: string[]
}

function flattenHeaders(
    headers: Record<string, HeaderValue>,
    stripUserHeaders = false
): Record<string, string> {
    const result: Record<string, string> = {}
    for (const [key, value] of Object.entries(headers)) {
        if (stripUserHeaders && key.toLowerCase().startsWith('x-user-')) continue
        if (typeof value === 'string') result[key] = value
        else if (Array.isArray(value) && value.length > 0) result[key] = String(value[0])
    }
    return result
}

function outgoingHeaders(request: ProxyRequest) {
    const traceId =
        normalizeUUID(request.headers['x-trace-id'] as string | undefined) || generateUUID()
    return getOutgoingRequestHeaders(traceId)
}

function accessToken(request: ProxyRequest, config: Config): string | undefined {
    return parseCookies(request.headers.cookie as string | undefined)[config.accessCookieName]
}

function injectIdentity(
    headers: Record<string, string>,
    request: ProxyRequest,
    access: string | undefined
): void {
    if (access) {
        headers.authorization = `Bearer ${access}`
        const userId = decodeJwtUserId(access)
        if (userId) headers['X-User-Id'] = userId
    }
    if (request.permissionsIsSuperadmin !== undefined) {
        headers['X-User-Is-Superadmin'] = request.permissionsIsSuperadmin ? 'true' : 'false'
        headers['X-User-System-Permissions'] = request.permissionsSystemPerms ?? ''
        headers['X-User-Permissions'] = request.permissionsProjectPerms ?? ''
    }
}

async function registerSpecialRoutes(app: Parameters<FastifyPluginAsync>[0], config: Config) {
    app.all('/api/config-service/v1/configs/bulk', async (_request, reply) => {
        return reply.status(404).send({ error: 'Not Found' })
    })

    app.get<{ Params: { sensorId: string } }>(
        '/api/v1/sensors/:sensorId/error-log',
        async (request, reply) => {
            const access = accessToken(request, config)
            const tracing = outgoingHeaders(request)
            const queryIndex = request.url.indexOf('?')
            const search = queryIndex >= 0 ? request.url.slice(queryIndex) : ''
            const target =
                `${config.targetTelemetryUrl}/api/v1/sensors/` +
                `${encodeURIComponent(request.params.sensorId)}/error-log${search}`
            const headers: Record<string, string> = {
                'X-Trace-Id': tracing['X-Trace-Id'],
                'X-Request-Id': tracing['X-Request-Id'],
            }
            if (access) headers.authorization = `Bearer ${access}`
            if (typeof request.headers.accept === 'string') {
                headers.accept = request.headers.accept
            }

            const response = await fetch(target, { headers })
            reply.code(response.status)
            const contentType = response.headers.get('content-type')
            if (contentType) reply.header('content-type', contentType)
            return reply.send(Buffer.from(await response.arrayBuffer()))
        }
    )
}

async function registerTelemetryRoute(
    app: Parameters<FastifyPluginAsync>[0],
    route: ProxyRouteDefinition,
    config: Config
) {
    await app.register(httpProxy, {
        prefix: route.prefix,
        upstream: route.upstream,
        rewritePrefix: route.rewritePrefix,
        http2: false,
        replyOptions: {
            rewriteRequestHeaders: (request, headers) => {
                const tracing = outgoingHeaders(request)
                const result = flattenHeaders(headers)
                result['X-Trace-Id'] = tracing['X-Trace-Id']
                result['X-Request-Id'] = tracing['X-Request-Id']

                const isRead =
                    request.url.startsWith('/api/v1/telemetry/stream') ||
                    request.url.startsWith('/api/v1/telemetry/query')
                if (isRead && !result.authorization) {
                    const access = accessToken(request, config)
                    if (access) result.authorization = `Bearer ${access}`
                }
                delete result.cookie
                return result
            },
        },
    })
}

async function registerIdentityRoute(
    app: Parameters<FastifyPluginAsync>[0],
    route: ProxyRouteDefinition,
    config: Config,
    stripUserHeaders = false
) {
    await app.register(httpProxy, {
        prefix: route.prefix,
        upstream: route.upstream,
        rewritePrefix: route.rewritePrefix,
        http2: false,
        replyOptions: {
            rewriteRequestHeaders: (request, headers) => {
                const tracing = outgoingHeaders(request)
                const result = flattenHeaders(headers, stripUserHeaders)
                result['X-Trace-Id'] = tracing['X-Trace-Id']
                result['X-Request-Id'] = tracing['X-Request-Id']
                injectIdentity(result, request, accessToken(request, config))
                delete result.cookie
                return result
            },
        },
    })
}

async function registerExperimentRoute(
    app: Parameters<FastifyPluginAsync>[0],
    route: ProxyRouteDefinition,
    config: Config
) {
    await app.register(httpProxy, {
        prefix: route.prefix,
        upstream: route.upstream,
        rewritePrefix: route.rewritePrefix,
        http2: false,
        websocket: route.websocket === true,
        replyOptions: {
            rewriteRequestHeaders: (request, headers) => {
                const tracing = outgoingHeaders(request)
                const result = flattenHeaders(headers)
                result['X-Trace-Id'] = tracing['X-Trace-Id']
                result['X-Request-Id'] = tracing['X-Request-Id']
                injectIdentity(result, request, accessToken(request, config))

                const projectId =
                    extractProjectId(request) ||
                    (typeof request.bodyProjectId === 'string'
                        ? request.bodyProjectId
                        : null)
                if (projectId) result['X-Project-Id'] = projectId
                if (request.allProjectIds?.length) {
                    result['X-Project-Ids'] = request.allProjectIds.join(',')
                }
                return result
            },
        },
    })
}

export const proxyRoutesPlugin: FastifyPluginAsync<ProxyRoutesOptions> = async (
    app,
    { config }
) => {
    await registerSpecialRoutes(app, config)

    for (const route of buildProxyRouteTable(config)) {
        switch (route.kind) {
            case 'auth-web':
            case 'auth-api':
                await registerAuthProxy(app, {
                    prefix: route.prefix,
                    upstream: route.upstream,
                    rewritePrefix: route.rewritePrefix,
                    accessCookieName: config.accessCookieName,
                    deleteCookie: route.deleteCookie,
                    ensureJsonContentType: route.ensureJsonContentType,
                })
                break
            case 'telemetry':
                await registerTelemetryRoute(app, route, config)
                break
            case 'script':
                await registerIdentityRoute(app, route, config)
                break
            case 'config-service':
                await registerIdentityRoute(app, route, config, true)
                break
            case 'experiment-service':
                await registerExperimentRoute(app, route, config)
                break
        }
    }
}
