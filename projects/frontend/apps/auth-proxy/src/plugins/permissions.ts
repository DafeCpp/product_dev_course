import type { FastifyRequest } from 'fastify'
import fp from 'fastify-plugin'
import { PassThrough } from 'stream'
import type { Config } from '../config'
import {
    type EffectivePermissions,
    type PermissionsCache,
} from '../permissions/cache'
import {
    decodeJwtPayload,
    getOutgoingRequestHeaders,
    parseCookies,
} from '../security'

const PERMISSIONS_TTL_SEC = 30

interface PermissionsPluginOptions {
    config: Config
    cache: PermissionsCache
}

interface JwtRbacClaims {
    user_id: string
    is_superadmin: boolean
    system_permissions: string[]
}

export function extractProjectId(request: Pick<FastifyRequest, 'query'>): string | null {
    const query = request.query as Record<string, string | string[]> | undefined
    const value = query?.project_id
    if (!value) return null
    return Array.isArray(value) ? value[0] : value
}

function extractProjectIdFromBody(request: FastifyRequest): string | null {
    if (request.rawBodyForProjectId) {
        try {
            const parsed = JSON.parse(request.rawBodyForProjectId) as { project_id?: unknown }
            return typeof parsed.project_id === 'string' ? parsed.project_id : null
        } catch {
            return null
        }
    }

    const body: unknown = request.body
    if (!body) return null
    if (typeof body === 'object' && !Buffer.isBuffer(body) && !Array.isArray(body)) {
        const projectId = (body as { project_id?: unknown }).project_id
        return typeof projectId === 'string' ? projectId : null
    }

    if (!String(request.headers['content-type'] || '').toLowerCase().includes('application/json')) {
        return null
    }
    try {
        const raw = Buffer.isBuffer(body) ? body.toString('utf-8') : String(body)
        if (!raw) return null
        const parsed = JSON.parse(raw) as { project_id?: unknown }
        return typeof parsed.project_id === 'string' ? parsed.project_id : null
    } catch {
        return null
    }
}

function getRbacClaims(token: string): JwtRbacClaims | null {
    const payload = decodeJwtPayload(token)
    if (!payload) return null
    const userId = payload.sub ?? payload.user_id
    if (typeof userId !== 'string') return null
    return {
        user_id: userId,
        is_superadmin: payload.sa === true,
        system_permissions: Array.isArray(payload.sys)
            ? payload.sys.filter((value): value is string => typeof value === 'string')
            : [],
    }
}

function cacheKey(userId: string, projectId?: string): string {
    return projectId ? `perms:${userId}:${projectId}` : `perms:sys:${userId}`
}

async function getEffectivePermissions(
    app: FastifyRequest['server'],
    config: Config,
    cache: PermissionsCache,
    userId: string,
    accessToken: string,
    projectId?: string
): Promise<EffectivePermissions | null> {
    const key = cacheKey(userId, projectId)
    const cached = await cache.get(key)
    if (cached) return cached

    try {
        const url = projectId
            ? `${config.authUrl}/api/v1/users/${userId}/effective-permissions?project_id=${projectId}`
            : `${config.authUrl}/api/v1/users/${userId}/effective-permissions`
        const response = await fetch(url, {
            headers: { Authorization: `Bearer ${accessToken}` },
            signal: AbortSignal.timeout(3000),
        })
        if (!response.ok) return null
        const permissions = (await response.json()) as EffectivePermissions
        await cache.set(key, permissions, PERMISSIONS_TTL_SEC)
        return permissions
    } catch (error) {
        app.log.error(
            {
                user_id: userId,
                project_id: projectId,
                error: error instanceof Error ? error.message : String(error),
            },
            'Error fetching effective permissions'
        )
        return null
    }
}

export const permissionsPlugin = fp<PermissionsPluginOptions>(async (app, { config, cache }) => {
    app.addHook('preParsing', async (request, _reply, payload) => {
        if (!['POST', 'PUT', 'PATCH'].includes(request.method.toUpperCase())) return payload
        if (!request.url.startsWith('/api/')) return payload
        if (!String(request.headers['content-type'] || '').toLowerCase().includes('application/json')) {
            return payload
        }

        const chunks: Buffer[] = []
        await new Promise<void>((resolve, reject) => {
            payload.on('data', (chunk: Buffer | string) => {
                chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk))
            })
            payload.once('end', resolve)
            payload.once('error', reject)
        })

        const body = Buffer.concat(chunks)
        request.rawBodyForProjectId = body.toString('utf-8')
        const replacement = new PassThrough()
        replacement.end(body)
        return replacement
    })

    app.addHook('preHandler', async (request) => {
        if (!request.url.startsWith('/api/')) return

        let projectId = extractProjectIdFromBody(request)
        if (projectId) request.bodyProjectId = projectId
        else projectId = extractProjectId(request)

        const access = parseCookies(request.headers.cookie as string | undefined)[
            config.accessCookieName
        ]
        if (access) {
            const claims = getRbacClaims(access)
            if (claims?.is_superadmin) {
                request.permissionsIsSuperadmin = true
                request.permissionsSystemPerms = ''
                request.permissionsProjectPerms = ''
            } else if (claims && projectId) {
                const permissions = await getEffectivePermissions(
                    app,
                    config,
                    cache,
                    claims.user_id,
                    access,
                    projectId
                )
                request.permissionsIsSuperadmin = false
                request.permissionsSystemPerms = permissions
                    ? permissions.system_permissions.join(',')
                    : claims.system_permissions.join(',')
                request.permissionsProjectPerms = permissions
                    ? permissions.project_permissions.join(',')
                    : ''
                if (!permissions) {
                    app.log.warn(
                        { project_id: projectId, user_id: claims.user_id, url: request.url },
                        'Failed to fetch effective permissions for project'
                    )
                }
            } else if (claims) {
                request.permissionsIsSuperadmin = false
                request.permissionsSystemPerms = claims.system_permissions.join(',')
                request.permissionsProjectPerms = ''
            }
        }

        const isAllSensorsRequest =
            !projectId &&
            request.method.toUpperCase() === 'GET' &&
            request.url.startsWith('/api/v1/sensors')
        if (!isAllSensorsRequest || !access) return

        try {
            const response = await fetch(`${config.authUrl}/projects`, {
                headers: {
                    authorization: `Bearer ${access}`,
                    ...getOutgoingRequestHeaders(request.traceId || ''),
                },
            })
            if (response.ok) {
                const data = (await response.json()) as { projects?: Array<{ id: string }> }
                request.allProjectIds = (data.projects ?? []).map((project) => project.id)
            }
        } catch (err) {
            app.log.warn(
                { err: err instanceof Error ? err.message : String(err), url: request.url },
                'Failed to fetch user projects for X-Project-Ids'
            )
        }
    })

    app.addHook('onSend', async (request, reply, payload) => {
        if (
            (reply.statusCode === 401 || reply.statusCode === 403) &&
            request.url.startsWith('/api/')
        ) {
            const access = parseCookies(request.headers.cookie as string | undefined)[
                config.accessCookieName
            ]
            const claims = access ? getRbacClaims(access) : null
            if (claims) cache.invalidateUser(claims.user_id).catch(() => {})
        }
        return payload
    })
}, { name: 'permissions', dependencies: ['request-context'] })
