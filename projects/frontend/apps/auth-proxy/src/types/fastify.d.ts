import 'fastify'

declare module 'fastify' {
    interface FastifyRequest {
        traceId?: string
        requestId?: string
        rawBodyForProjectId?: string
        bodyProjectId?: string
        permissionsIsSuperadmin?: boolean
        permissionsSystemPerms?: string
        permissionsProjectPerms?: string
        allProjectIds?: string[]
    }
}
